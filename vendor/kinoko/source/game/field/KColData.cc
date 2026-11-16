#include "KColData.hh"

#include <egg/geom/Sphere.hh>
#include <egg/math/Math.hh>

#include <algorithm>
#include <cmath>
#include <bit>
#include <cstring>

// Credit: em-eight/mkw
// Credit: stblr/Hanachan

namespace Kinoko::Field {

/// @addr{0x807BDC5C}
KColData::KColData(const void *file) {
    auto addOffset = [](const void *file, u32 offset) -> const void * {
        return reinterpret_cast<const void *>(reinterpret_cast<const u8 *>(file) + offset);
    };
    EGG::RamStream stream = EGG::RamStream(file, sizeof(KColHeader));

    u32 posOffset = stream.read_u32();
    u32 nrmOffset = stream.read_u32();
    u32 prismOffset = stream.read_u32();
    u32 blockOffset = stream.read_u32();

    m_posData = addOffset(file, posOffset);
    m_nrmData = addOffset(file, nrmOffset);
    m_prismData = addOffset(file, prismOffset);
    m_blockData = addOffset(file, blockOffset);

    m_prismThickness = stream.read_f32();
    m_areaMinPos.read(stream);
    m_areaXWidthMask = stream.read_u32();
    m_areaYWidthMask = stream.read_u32();
    m_areaZWidthMask = stream.read_u32();
    m_blockWidthShift = stream.read_u32();
    m_areaXBlocksShift = stream.read_u32();
    m_areaXYBlocksShift = stream.read_u32();
    m_sphereRadius = stream.read_f32();

    m_pos.setZero();
    m_prevPos.setZero();
    m_movement.setZero();
    m_radius = 0.0f;
    m_prismIter = nullptr;
    m_cachedPrismArray = m_prismCache.data();
    m_prismCacheTop = m_prismCache.data() + 1;
    m_cachedPos.setZero();

    // NOTE: Collision is expensive on the CPU, so we preload all of the prism data to ensure we're
    // not constantly handling endianness.
    preloadPrisms();
    preloadNormals();
    preloadVertices();
    packPrisms();

    computeBBox();
}

KColData::~KColData() = default;

/// @addr{0x807C24C0}
void KColData::narrowScopeLocal(const EGG::Vector3f &pos, f32 radius, KCLTypeMask mask) {
    m_prismCacheTop = m_prismCache.data() + 1;
    ++m_cacheGen;
    m_pos = pos;
    m_radius = radius;
    m_typeMask = mask;
    m_cachedPos = pos;
    m_cachedRadius = radius;

    if (radius <= m_sphereRadius) {
        narrowPolygon_EachBlock(searchBlock(pos));
    } else {
        // Retail RMCU 807b3ac8 calls the multi-block traversal (807af6cc).
        // A sphere larger than the KCL's padded-leaf radius cannot use just
        // its centre leaf. The previous port left its cache empty instead.
        const f32 center[] = {pos.x, pos.y, pos.z};
        const f32 areaMin[] = {m_areaMinPos.x, m_areaMinPos.y, m_areaMinPos.z};
        const u32 masks[] = {m_areaXWidthMask, m_areaYWidthMask, m_areaZWidthMask};
        s32 low[3], high[3];
        bool outside = false;
        for (unsigned i = 0; i < 3; ++i) {
            low[i] = std::max(s32((center[i] - radius) - areaMin[i]), s32(0));
            high[i] = std::min(s32((center[i] + radius) - areaMin[i]), s32(~masks[i]));
            outside |= low[i] >= high[i];
        }
        if (!outside) {
            const auto *blocks = static_cast<const u8 *>(m_blockData);
            auto visit = [&](auto &&self, const u8 *table, u32 index,
                                 u32 x, u32 y, u32 z, u32 size) -> void {
                if (m_prismCacheTop == m_prismCache.end() - 1 ||
                        x > u32(high[0]) || x + size <= u32(low[0]) ||
                        y > u32(high[1]) || y + size <= u32(low[1]) ||
                        z > u32(high[2]) || z + size <= u32(low[2])) return;
                const u32 offset = parse<u32>(*reinterpret_cast<const u32 *>(table + index * 4));
                if (offset & 0x80000000u) {
                    narrowPolygon_EachBlock(reinterpret_cast<const u16 *>(table + (offset & 0x7fffffffu)));
                    return;
                }
                const u32 half = size / 2;
                for (u32 child = 0; child < 8; ++child)
                    self(self, table + offset, child, x + ((child & 1) ? half : 0),
                            y + ((child & 2) ? half : 0), z + ((child & 4) ? half : 0), half);
            };
            const u32 size = 1u << m_blockWidthShift;
            // Retail visits root cells in X/Y/Z order, child octants 0..7.
            for (u32 x = u32(low[0]) / size; x <= u32(high[0]) / size; ++x)
                for (u32 y = u32(low[1]) / size; y <= u32(high[1]) / size; ++y)
                    for (u32 z = u32(low[2]) / size; z <= u32(high[2]) / size; ++z)
                        visit(visit, blocks, x | (y << m_areaXBlocksShift) | (z << m_areaXYBlocksShift),
                                x * size, y * size, z * size, size);
        }
    }

    *m_prismCacheTop = 0;
}

/// @addr{0x807C243C}
void KColData::narrowPolygon_EachBlock(const u16 *prismArray) {
    m_prismIter = prismArray;

    while (checkSphereSingle(nullptr, nullptr, nullptr)) {
        /// We assume the cache has same endianness as the archive file,
        /// so do not parse out the prism index and directly store it in the cache.
        *(m_prismCacheTop++) = *m_prismIter;

        if (m_prismCacheTop == m_prismCache.end()) {
            --m_prismCacheTop;
            return;
        }
    }
}

/// @brief Calculates a EGG::BoundBox3f that describes the boundary of the track's KCL
/// @addr{0x807BDDFC}
void KColData::computeBBox() {
    m_bbox.max.set(-999999.0f);
    m_bbox.min.set(999999.0f);

    for (size_t i = 1; i < m_prisms.size(); ++i) {
        const auto &prism = m_prisms[i];
        const EGG::Vector3f &fnrm = m_nrms[prism.fnrm_i];
        const EGG::Vector3f &enrm1 = m_nrms[prism.enrm1_i];
        const EGG::Vector3f &enrm2 = m_nrms[prism.enrm2_i];
        const EGG::Vector3f &enrm3 = m_nrms[prism.enrm3_i];
        const EGG::Vector3f &vtx1 = m_vertices[prism.pos_i];

        const EGG::Vector3f vtx2 = GetVertex(prism.height, vtx1, fnrm, enrm3, enrm1);
        const EGG::Vector3f vtx3 = GetVertex(prism.height, vtx1, fnrm, enrm3, enrm2);

        m_bbox.min = m_bbox.min.minimize(vtx1);
        m_bbox.min = m_bbox.min.minimize(vtx2);
        m_bbox.min = m_bbox.min.minimize(vtx3);
        m_bbox.max = m_bbox.max.maximize(vtx1);
        m_bbox.max = m_bbox.max.maximize(vtx2);
        m_bbox.max = m_bbox.max.maximize(vtx3);
    }
}

/// @addr{0x807C1F80}
bool KColData::checkPointCollision(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    return std::isfinite(m_prevPos.y) ? checkPointMovement(distOut, fnrmOut, flagsOut) :
                                        checkPoint(distOut, fnrmOut, flagsOut);
}

/// @addr{0x807C2410}
bool KColData::checkSphereCollision(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    return std::isfinite(m_prevPos.y) ? checkSphereMovement(distOut, fnrmOut, flagsOut) :
                                        checkSphere(distOut, fnrmOut, flagsOut);
}

/// @brief Iterates the list of looked-up triangles to see if we are colliding
/// @addr{0x807C1514}
/// @param distOut If colliding, returns the distance between the player and the triangle
/// @param fnrmOut If colliding, returns the floor normal of the triangle
/// @param flagsOut If colliding, returns the KCL attributes for that triangle
/// @return whether or not the player is colliding with the triangle
bool KColData::checkSphere(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    // If there's no list of triangles to check, there's no collision
    if (!m_prismIter) {
        return false;
    }

    // Check collision for all triangles, and continuously call the function until we're out
    while (*++m_prismIter != 0) {
        PackedPrism scratch;
        const PackedPrism *at = prismAt(parse<u16>(*m_prismIter), scratch);
        if (!at) {
            continue;
        }
        const PackedPrism &prism = *at;
        if (checkCollision<CollisionCheckType::Plane>(prism, distOut, fnrmOut, flagsOut)) {
            return true;
        }
    }

    // We're out of triangles to check - another list must be prepared for subsequent calls
    m_prismIter = nullptr;
    return false;
}

/// @addr{0x807C0F00}
bool KColData::checkSphereSingle(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    if (!m_prismIter) {
        return false;
    }

    while (*++m_prismIter != 0) {
        // The old reverse scan dereferenced begin()-1 after a miss at
        // the first entry. Never form or read a pointer before this array.
        const auto begin = m_prismCache.begin() + 1;
        if (std::find(begin, m_prismCacheTop, *m_prismIter) != m_prismCacheTop) {
            continue;
        }

        PackedPrism scratch;
        const PackedPrism *at = prismAt(parse<u16>(*m_prismIter), scratch);
        if (!at) {
            continue;
        }
        const PackedPrism &prism = *at;
        if (checkCollision<CollisionCheckType::Edge>(prism, distOut, fnrmOut, flagsOut)) {
            return true;
        }
    }

    m_prismIter = nullptr;
    return false;
}

/// @brief Sets members in preparation of a subsequent point collision check call
/// @addr{0x807C1B0C}
void KColData::lookupPoint(const EGG::Vector3f &pos, const EGG::Vector3f &prevPos,
        KCLTypeMask typeMask) {
    m_prismIter = searchBlock(pos);
    m_pos = pos;
    m_prevPos = prevPos;
    m_movement = pos - prevPos;
    m_typeMask = typeMask;
}

/// @brief Sets members in preparation of a subsequent sphere collision check call
/// @addr{0x807C1BB4}
void KColData::lookupSphere(f32 radius, const EGG::Vector3f &pos, const EGG::Vector3f &prevPos,
        KCLTypeMask typeMask) {
    m_prismIter = searchBlock(pos);
    m_pos = pos;
    m_prevPos = prevPos;
    m_movement = pos - prevPos;
    m_radius = std::min(radius, m_sphereRadius);
    m_typeMask = typeMask;
}

/// @addr{0x807C1DE8}
void KColData::lookupSphereCached(const EGG::Vector3f &p1, const EGG::Vector3f &p2, u32 typeMask,
        f32 radius) {
    EGG::Sphere3f sphere1(p1, radius);
    EGG::Sphere3f sphere2(m_cachedPos, m_cachedRadius);

    if (!sphere1.isInsideOtherSphere(sphere2)) {
        m_prismIter = searchBlock(p1);
        m_radius = std::min(m_sphereRadius, radius);
    } else {
        m_radius = radius;
        m_prismIter = m_cachedPrismArray;
    }

    m_pos = p1;
    m_prevPos = p2;
    m_movement = p1 - p2;
    m_typeMask = typeMask;
}

namespace {

/// mkw-3ds: the one batch (KColData::batchSpheresCached). Racers step one at a
/// time, and only the course KCL batches.
struct SphereBatch {
    struct Hit {
        f32 dist;
        const EGG::Vector3f *fnrm;
        u16 attribute;
    };
    struct Entry {
        EGG::Vector3f pos;
        EGG::Vector3f prevPos;
        EGG::Vector3f movement;
        f32 radius;
        KCLTypeMask typeMask;
        u16 count;
        bool valid;
        bool movementCheck;
        Hit hits[KColData::BATCH_HITS];
    };
    const KColData *owner = nullptr;
    u32 gen = 0;
    size_t count = 0;
    size_t next = 0; ///< The entry the next query most likely asks (batch order).
    const Entry *replay = nullptr;
    u16 replayIndex = 0;
    Entry entries[KColData::BATCH_MAX];
};

SphereBatch s_batch;

bool sameBits(const EGG::Vector3f &a, const EGG::Vector3f &b) {
    return std::bit_cast<u32>(a.x) == std::bit_cast<u32>(b.x) &&
            std::bit_cast<u32>(a.y) == std::bit_cast<u32>(b.y) &&
            std::bit_cast<u32>(a.z) == std::bit_cast<u32>(b.z);
}

} // namespace

u32 KColData::s_batchedSpheres = 0;
u32 KColData::s_batchReplays = 0;

void KColData::ClearBatch() {
    s_batch.owner = nullptr;
    s_batch.count = 0;
    s_batch.replay = nullptr;
}

void KColData::batchSpheresCached(const BatchSphere *spheres, size_t count) {
    count = std::min(count, BATCH_MAX);
    s_batch.owner = this;
    s_batch.gen = m_cacheGen;
    s_batch.count = count;
    s_batch.next = 0;
    s_batch.replay = nullptr;

    // lookupSphereCached's choice, per sphere: only spheres inside the narrowed
    // prism cache's sphere are batched (they all read its list); the rest run
    // their own query.
    size_t batched[BATCH_MAX];
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        const BatchSphere &in = spheres[i];
        SphereBatch::Entry &e = s_batch.entries[i];
        EGG::Sphere3f sphere1(in.pos, in.radius);
        EGG::Sphere3f sphere2(m_cachedPos, m_cachedRadius);
        e.valid = sphere1.isInsideOtherSphere(sphere2);
        if (!e.valid) {
            continue;
        }
        e.radius = in.radius;
        e.pos = in.pos;
        e.prevPos = in.prevPos;
        e.movement = in.pos - in.prevPos;
        e.typeMask = in.typeMask;
        e.count = 0;
        e.movementCheck = std::isfinite(in.prevPos.y);
        batched[n++] = i;
    }
    s_batchedSpheres += n;
    if (n == 0) {
        return;
    }

    // One pass over the prism list, every batched sphere tested per prism
    // (checkSphere / checkSphereMovement's loop, in the same order).
    for (const u16 *it = m_cachedPrismArray; *++it != 0;) {
        const PackedPrism &prism = m_packed[parse<u16>(*it)];
        const u32 attributeMask = KCL_ATTRIBUTE_TYPE_BIT(prism.attribute);
        for (size_t k = 0; k < n; ++k) {
            SphereBatch::Entry &e = s_batch.entries[batched[k]];
            if (!(attributeMask & e.typeMask)) {
                continue;
            }
            const QueryRef q{e.pos, e.movement, e.radius, e.typeMask};
            f32 dist;
            const bool hit = e.movementCheck ?
                    checkCollision<CollisionCheckType::Movement>(q, prism, &dist, nullptr, nullptr) :
                    checkCollision<CollisionCheckType::Plane>(q, prism, &dist, nullptr, nullptr);
            if (!hit) {
                continue;
            }
            if (e.count == BATCH_HITS) {
                e.valid = false;
                continue;
            }
            e.hits[e.count++] = SphereBatch::Hit{dist, &prism.fnrm, prism.attribute};
        }
    }
}

bool KColData::replayBatched() {
    if (s_batch.owner != this || s_batch.gen != m_cacheGen ||
            m_prismIter != m_cachedPrismArray) {
        return false;
    }
    auto matches = [&](const SphereBatch::Entry &e) {
        return e.valid && e.typeMask == m_typeMask &&
                std::bit_cast<u32>(e.radius) == std::bit_cast<u32>(m_radius) &&
                sameBits(e.pos, m_pos) && sameBits(e.prevPos, m_prevPos);
    };
    size_t i = s_batch.next;
    if (i >= s_batch.count || !matches(s_batch.entries[i])) {
        for (i = 0; i < s_batch.count && !matches(s_batch.entries[i]); ++i) {
        }
        if (i == s_batch.count) {
            return false;
        }
    }
    s_batch.next = i + 1;
    s_batch.replay = &s_batch.entries[i];
    s_batch.replayIndex = 0;
    ++s_batchReplays;
    return true;
}

/// mkw-3ds: checkSphereCollision for a batched query: the kept hits, then false
/// with m_prismIter cleared, as the prism loop ends.
bool KColData::checkSphereReplay(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    const SphereBatch::Entry *e = s_batch.replay;
    if (e && s_batch.replayIndex < e->count) {
        const SphereBatch::Hit &hit = e->hits[s_batch.replayIndex++];
        if (distOut) {
            *distOut = hit.dist;
        }
        if (fnrmOut) {
            *fnrmOut = *hit.fnrm;
        }
        if (flagsOut) {
            *flagsOut = hit.attribute;
        }
        return true;
    }
    s_batch.replay = nullptr;
    m_prismIter = nullptr;
    return false;
}

/// @brief Finds the data block corresponding to the provided position
/// @addr{0x807BE030}
/// @param point The player's position
/// @return the address of the leaf node containing the input point.
const u16 *KColData::searchBlock(const EGG::Vector3f &point) {
    // Calculate the x, y, and z offsets of the point from the minimum
    // corner of the tree's bounding box.
    const int x = point.x - m_areaMinPos.x;
    const int y = point.y - m_areaMinPos.y;
    const int z = point.z - m_areaMinPos.z;

    // Check if the point is outside the tree's bounding box in the x, y,
    // or z dimensions. If it is, return 0.
    if (x & m_areaXWidthMask || y & m_areaYWidthMask || z & m_areaZWidthMask) {
        return nullptr;
    }

    // Initialize the current tree node to the root node of the tree.
    u32 shift = m_blockWidthShift;
    const u8 *curBlock = reinterpret_cast<const u8 *>(m_blockData);
    s32 offset;

    // Traverse the tree to find the leaf node containing the input point.
    u32 index = 4 *
            (((u32)z >> shift) << m_areaXYBlocksShift | ((u32)y >> shift) << m_areaXBlocksShift |
                    (u32)x >> shift);

    while (true) {
        // Get the offset of the current node's child node.
        offset = parse<u32>(*reinterpret_cast<const u32 *>(curBlock + index));

        // If the offset is negative, the current node is a leaf node.
        if ((offset & 0x80000000) != 0) {
            break;
        }

        // If the offset is non-negative, update the current node to be
        // the child node and continue traversing the tree.
        shift--;
        curBlock += offset;

        u32 x_shift = ((1 * ((u32)x >> shift)) & 1);
        u32 y_shift = ((2 * ((u32)y >> shift)) & 2);
        u32 z_shift = ((4 * ((u32)z >> shift)) & 4);

        index = 4 * (x_shift | y_shift | z_shift);
    }

    // We have to remove the MSB since it's solely used to identify leaves.
    return reinterpret_cast<const u16 *>(curBlock + (offset & ~0x80000000));
}

/// @brief Computes a prism vertex based off of the triangle's normal vectors
/// @addr{0x807BDF54}
/// @par Triangle Vertices Formula
/// Given a triangle with vertices \f$\vec{A}, \vec{B}, \vec{C}\f$, face normal \f$\hat{f}\f$, and
/// height \f$h\f$, label the edge normals by: \begin{aligned}\hat{en}_1 := e_{AB}, \,\,
/// \hat{en}_2:= e_{AC}, \,\,\hat{en}_3:=e_{BC} \end{aligned} We can recover \f$\vec{B}, \vec{C}\f$
/// via: \begin{aligned} \vec{B} = \vec{A} + \dfrac{h}{(\hat{en}_2 \times \hat{f}) \cdot
/// \hat{en}_3}\left(\hat{en}_2 \times \hat{f}\right), \, \, \vec{C} = \vec{A} +
/// \dfrac{h}{(\hat{en}_1 \times \hat{f}) \cdot \hat{en}_3}(\hat{en}_1 \times \hat{f}) \, .
/// \end{aligned}
EGG::Vector3f KColData::GetVertex(f32 height, const EGG::Vector3f &vertex1,
        const EGG::Vector3f &fnrm, const EGG::Vector3f &enrm3, const EGG::Vector3f &enrm) {
    EGG::Vector3f cross = fnrm.cross(enrm);
    f32 dp = cross.ps_dot(enrm3);
    cross *= (height / dp);

    return cross + vertex1;
}

/// @brief Creates a copy of the prisms in memory.
/// @details Optimizes for time by copying all of the prisms to avoid constant byteswapping.
/// Memory cost is typically upwards of a few hundred KB, with the worst case being ~1MB.
void KColData::preloadPrisms() {
    size_t prismCount =
            (reinterpret_cast<uintptr_t>(m_blockData) - reinterpret_cast<uintptr_t>(m_prismData)) /
            sizeof(KCollisionPrism);

    EGG::RamStream stream = EGG::RamStream(m_prismData, sizeof(KCollisionPrism) * prismCount);

    m_prisms = owning_span<KCollisionPrism>(prismCount);

    // Because the prisms are one-indexed, we insert an empty prism
    stream.skip(sizeof(KCollisionPrism));

    for (size_t i = 1; i < prismCount; ++i) {
        KCollisionPrism &prism = m_prisms[i];
        prism.height = stream.read_f32();
        prism.pos_i = stream.read_u16();
        prism.fnrm_i = stream.read_u16();
        prism.enrm1_i = stream.read_u16();
        prism.enrm2_i = stream.read_u16();
        prism.enrm3_i = stream.read_u16();
        prism.attribute = stream.read_u16();
    }
}

bool KColData::s_packed = true;

/// mkw-3ds DIAG A/B: with s_packed off, the old layout's reads (the prism, then its
/// vertex and normals at their own addresses); nullptr when its type check rejects it,
/// as checkCollision would before reading anything else.
const KColData::PackedPrism *KColData::prismAt(u16 index, PackedPrism &scratch) const {
    if (s_packed) {
        return &m_packed[index];
    }
    const KCollisionPrism &prism = m_prisms[index];
    if (!(KCL_ATTRIBUTE_TYPE_BIT(prism.attribute) & m_typeMask)) {
        return nullptr;
    }
    scratch.attribute = prism.attribute;
    scratch.height = prism.height;
    scratch.vertex = m_vertices[prism.pos_i];
    scratch.enrm1 = m_nrms[prism.enrm1_i];
    scratch.enrm2 = m_nrms[prism.enrm2_i];
    scratch.enrm3 = m_nrms[prism.enrm3_i];
    scratch.fnrm = m_nrms[prism.fnrm_i];
    return &scratch;
}

/// mkw-3ds: copies each prism's vertex and normals next to it (see PackedPrism).
void KColData::packPrisms() {
    m_packed = owning_span<PackedPrism>(m_prisms.size());
    for (size_t i = 1; i < m_prisms.size(); ++i) {
        const KCollisionPrism &prism = m_prisms[i];
        PackedPrism &packed = m_packed[i];
        packed.vertex = m_vertices[prism.pos_i];
        packed.enrm1 = m_nrms[prism.enrm1_i];
        packed.enrm2 = m_nrms[prism.enrm2_i];
        packed.enrm3 = m_nrms[prism.enrm3_i];
        packed.fnrm = m_nrms[prism.fnrm_i];
        packed.height = prism.height;
        packed.attribute = prism.attribute;
        packed.pad = 0;
    }
}

/// @brief Creates a copy of the normals in memory.
/// @details Optimizes for time by copying all of the normals to avoid constant byteswapping.
/// Memory cost is typically upwards of a few hundred KB, with the worst case being ~750KB.
void KColData::preloadNormals() {
    size_t normalCount = (reinterpret_cast<uintptr_t>(m_prismData) + sizeof(KCollisionPrism) -
                                 reinterpret_cast<uintptr_t>(m_nrmData)) /
            sizeof(EGG::Vector3f);

    m_nrms = owning_span<EGG::Vector3f>(normalCount);
    EGG::RamStream stream = EGG::RamStream(m_nrmData, sizeof(EGG::Vector3f) * normalCount);

    for (auto &nrm : m_nrms) {
        nrm.read(stream);
    }
}

/// @brief Creates a copy of the vertices in memory.
/// @details Optimizes for time by copying all of the vertices to avoid constant byteswapping.
/// Memory cost is typically upwards of a few hundred KB, with the worst case being ~750KB.
void KColData::preloadVertices() {
    size_t vertexCount =
            (reinterpret_cast<uintptr_t>(m_nrmData) - reinterpret_cast<uintptr_t>(m_posData)) /
            sizeof(EGG::Vector3f);

    m_vertices = owning_span<EGG::Vector3f>(vertexCount);
    EGG::RamStream stream = EGG::RamStream(m_posData, sizeof(EGG::Vector3f) * vertexCount);

    for (auto &vert : m_vertices) {
        vert.read(stream);
    }
}

/// @brief This is a combination of the three collision checks in the base game.
/// @details The checks vary only by a few if-statements, related to whether we are checking for:
/// 1. A collision with at least the triangle edge (0x807C0F00)
/// 2. A collision with the triangle plane (0x807C1514)
/// 3. A collision such that we are inside the triangle (0x807C0884)
template <KColData::CollisionCheckType Type>
bool KColData::checkCollision(const QueryRef &q, const PackedPrism &prism, f32 *distOut,
        EGG::Vector3f *fnrmOut, u16 *flagsOut) {
    // Responsible for updating the output params
    auto out = [&](f32 dist) {
        if (distOut) {
            *distOut = dist;
        }
        if (fnrmOut) {
            *fnrmOut = prism.fnrm;
        }
        if (flagsOut) {
            *flagsOut = prism.attribute;
        }
        return true;
    };

    // The flag check occurs earlier than in the base game here. We don't want to do math if the tri
    // we're checking doesn't have matching flags.
    u32 attributeMask = KCL_ATTRIBUTE_TYPE_BIT(prism.attribute);
    if (!(attributeMask & q.typeMask)) {
        return false;
    }

    const EGG::Vector3f relativePos = q.pos - prism.vertex;

    // mkw-3ds: the four distances are independent, so all are computed before
    // the first exit (same expressions, same rounding): on the in-order VFP11
    // the four dependent chains overlap instead of each waiting for a compare.
    // Edge normals point outside the triangle
    const EGG::Vector3f &enrm1 = prism.enrm1;
    const EGG::Vector3f &enrm2 = prism.enrm2;
    const EGG::Vector3f &enrm3 = prism.enrm3;
    const EGG::Vector3f &fnrm = prism.fnrm;
    const f32 dist_ca = relativePos.ps_dot(enrm1);
    const f32 dist_ab = relativePos.ps_dot(enrm2);
    const f32 dist_bc = relativePos.ps_dot(enrm3) - prism.height;
    const f32 plane_dist = relativePos.ps_dot(fnrm);
    if (q.radius <= dist_ca) {
        return false;
    }

    if (q.radius <= dist_ab) {
        return false;
    }

    if (q.radius <= dist_bc) {
        return false;
    }

    f32 dist_in_plane = q.radius - plane_dist;
    if (dist_in_plane <= 0.0f) {
        return false;
    }

    f32 typeDistance = m_prismThickness;
    if constexpr (Type == CollisionCheckType::Edge) {
        typeDistance += q.radius;
    }

    if (dist_in_plane >= typeDistance) {
        return false;
    }

    if constexpr (Type == CollisionCheckType::Movement) {
        if (attributeMask & KCL_TYPE_DIRECTIONAL && q.movement.dot(fnrm) > 0.0f) {
            return false;
        }
    }

    // Originally part of the edge searching, but moved out for simplicity
    // If these are all zero, then we're inside the triangle
    if (dist_ab <= 0.0f && dist_bc <= 0.0f && dist_ca <= 0.0f) {
        if constexpr (Type == CollisionCheckType::Movement) {
            EGG::Vector3f lastPos = relativePos - q.movement;
            // We're only colliding if we are moving towards the face
            if (plane_dist < 0.0f && lastPos.ps_dot(fnrm) < 0.0f) {
                return false;
            }
        }
        return out(dist_in_plane);
    }

    EGG::Vector3f edge_nor, other_edge_nor;
    f32 edge_dist, other_edge_dist;
    bool swap = false;
    bool swapNorms = false;
    // > means further, < means closer, = means same distance
    if (dist_ab >= dist_ca && dist_ab > dist_bc) {
        // AB is the furthest edge
        edge_nor = enrm2;
        edge_dist = dist_ab;
        if (dist_ca >= dist_bc) {
            // CA is the second furthest edge
            other_edge_nor = enrm1;
            other_edge_dist = dist_ca;
            swapNorms = true;
        } else {
            // BC is the second furthest edge
            other_edge_nor = enrm3;
            other_edge_dist = dist_bc;
            swap = true;
        }
    } else if (dist_bc >= dist_ca) {
        // BC is the furthest edge
        edge_nor = enrm3;
        edge_dist = dist_bc;
        if (dist_ab >= dist_ca) {
            // AB is the second furthest edge
            other_edge_nor = enrm2;
            other_edge_dist = dist_ab;
            swapNorms = true;
        } else {
            // CA is the second furthest edge
            other_edge_nor = enrm1;
            other_edge_dist = dist_ca;
            swap = true;
        }
    } else {
        // CA is the furthest edge
        edge_nor = enrm1;
        edge_dist = dist_ca;
        if (dist_bc >= dist_ab) {
            // BC is the second furthest edge
            other_edge_nor = enrm3;
            other_edge_dist = dist_bc;
            swapNorms = true;
        } else {
            // AB is the second furthest edge
            other_edge_nor = enrm2;
            other_edge_dist = dist_ab;
            swap = true;
        }
    }

    f32 cos = edge_nor.ps_dot(other_edge_nor);
    f32 sq_dist;
    if (cos * edge_dist > other_edge_dist) {
        if constexpr (Type == CollisionCheckType::Plane) {
            if (edge_dist > plane_dist) {
                return false;
            }
        }
        sq_dist = q.radius * q.radius - edge_dist * edge_dist;
    } else {
        f32 sq_sin = cos * cos - 1.0f;

        if (swap) {
            std::swap(edge_dist, other_edge_dist);
        }

        if (swapNorms) {
            std::swap(edge_nor, other_edge_nor);
        }

        f32 t = (cos * edge_dist - other_edge_dist) / sq_sin;
        f32 s = edge_dist - t * cos;
        const EGG::Vector3f corner_pos = edge_nor * t + other_edge_nor * s;

        f32 cornerDot = corner_pos.ps_squareMag();
        if constexpr (Type == CollisionCheckType::Plane) {
            if (cornerDot > plane_dist * plane_dist) {
                return false;
            }
        }

        sq_dist = q.radius * q.radius - cornerDot;
    }

    if (sq_dist < plane_dist * plane_dist || sq_dist <= 0.0f) {
        return false;
    }

    f32 dist = EGG::Mathf::sqrt(sq_dist) - plane_dist;
    if (dist <= 0.0f) {
        return false;
    }

    if constexpr (Type == CollisionCheckType::Movement) {
        EGG::Vector3f lastPos = relativePos - q.movement;
        // We're only colliding if we are moving towards the face
        if (lastPos.ps_dot(fnrm) < 0.0f) {
            return false;
        }
    }

    return out(dist);
}

/// @brief This is a combination of two point collision check functions. They only vary based on
/// whether we are checking movement.
bool KColData::checkPointCollision(const PackedPrism &prism, f32 *distOut,
        EGG::Vector3f *fnrmOut, u16 *flagsOut, bool movement) {
    KCLTypeMask attrMask = KCL_ATTRIBUTE_TYPE_BIT(prism.attribute);
    if (!(attrMask & m_typeMask)) {
        return false;
    }

    const EGG::Vector3f relativePos = m_pos - prism.vertex;

    const EGG::Vector3f &enrm1 = prism.enrm1;
    const EGG::Vector3f &enrm2 = prism.enrm2;
    const EGG::Vector3f &enrm3 = prism.enrm3;
    const EGG::Vector3f &fnrm = prism.fnrm;
    const f32 dist_ca = relativePos.ps_dot(enrm1);
    const f32 dist_ab = relativePos.ps_dot(enrm2);
    const f32 dist_bc = relativePos.ps_dot(enrm3) - prism.height;
    const f32 plane_dist = relativePos.ps_dot(fnrm);
    if (dist_ca >= 0.01f) {
        return false;
    }

    if (dist_ab >= 0.01f) {
        return false;
    }

    if (dist_bc >= 0.01f) {
        return false;
    }

    f32 dist_in_plane = 0.01f - plane_dist;
    if (dist_in_plane <= 0.0f) {
        return false;
    }

    if (m_prismThickness <= dist_in_plane && 0.02f + m_prismThickness <= dist_in_plane) {
        return false;
    }

    if (movement && (attrMask & KCL_TYPE_DIRECTIONAL) && m_movement.dot(fnrm) < 0.0f) {
        return false;
    }

    if (distOut) {
        *distOut = dist_in_plane;
    }

    if (fnrmOut) {
        *fnrmOut = fnrm;
    }

    if (flagsOut) {
        *flagsOut = prism.attribute;
    }

    return true;
}

/// @brief Iterates the local data block to check for directional collision
/// @addr{0x807C0884}
/// @param distOut If colliding, returns the distance between the player and the tri
/// @param fnrmOut If colliding, returns the floor normal of the triangle
/// @param attributeOut If colliding, returns the KCL attributes for that triangle
/// @return Whether or not a collision has occurred
bool KColData::checkSphereMovement(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *attributeOut) {
    // If there's no list of triangles to check, there's no collision
    if (!m_prismIter) {
        return false;
    }

    // Check collision for all triangles, and continuously call the function until we're out
    while (*++m_prismIter != 0) {
        PackedPrism scratch;
        const PackedPrism *at = prismAt(parse<u16>(*m_prismIter), scratch);
        if (!at) {
            continue;
        }
        const PackedPrism &prism = *at;
        if (checkCollision<CollisionCheckType::Movement>(prism, distOut, fnrmOut, attributeOut)) {
            return true;
        }
    }

    // We're out of triangles to check - another list must be prepared for subsequent calls
    m_prismIter = nullptr;
    return false;
}

/// @addr{0x807C21F4}
bool KColData::checkPoint(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *attributeOut) {
    // If there's no list of triangles to check, there's no collision
    if (!m_prismIter) {
        return false;
    }

    // Check collision for all triangles, and continuously call the function until we're out
    while (*++m_prismIter != 0) {
        PackedPrism scratch;
        const PackedPrism *at = prismAt(parse<u16>(*m_prismIter), scratch);
        if (!at) {
            continue;
        }
        const PackedPrism &prism = *at;
        if (checkPointCollision(prism, distOut, fnrmOut, attributeOut, false)) {
            return true;
        }
    }

    // We're out of triangles to check - another list must be prepared for subsequent calls
    m_prismIter = nullptr;
    return false;
}

/// @addr{0x807C1F80}
bool KColData::checkPointMovement(f32 *distOut, EGG::Vector3f *fnrmOut, u16 *attributeOut) {
    // If there's no list of triangles to check, there's no collision
    if (!m_prismIter) {
        return false;
    }

    // Check collision for all triangles, and continuously call the function until we're out
    while (*++m_prismIter != 0) {
        PackedPrism scratch;
        const PackedPrism *at = prismAt(parse<u16>(*m_prismIter), scratch);
        if (!at) {
            continue;
        }
        const PackedPrism &prism = *at;
        if (checkPointCollision(prism, distOut, fnrmOut, attributeOut, true)) {
            return true;
        }
    }

    // We're out of triangles to check - another list must be prepared for subsequent calls
    m_prismIter = nullptr;
    return false;
}

KColData::KCollisionPrism::KCollisionPrism() = default;

KColData::KCollisionPrism::KCollisionPrism(f32 height, u16 posIndex, u16 faceNormIndex,
        u16 edge1NormIndex, u16 edge2NormIndex, u16 edge3NormIndex, u16 attribute)
    : height(height), pos_i(posIndex), fnrm_i(faceNormIndex), enrm1_i(edge1NormIndex),
      enrm2_i(edge2NormIndex), enrm3_i(edge3NormIndex), attribute(attribute) {}

void CollisionInfo::update(f32 now_dist, const EGG::Vector3f &offset, const EGG::Vector3f &fnrm,
        u32 kclAttributeTypeBit) {
    bbox.min = bbox.min.minimize(offset);
    bbox.max = bbox.max.maximize(offset);

    if (kclAttributeTypeBit & KCL_TYPE_FLOOR) {
        updateFloor(now_dist, fnrm);
    } else if (kclAttributeTypeBit & KCL_TYPE_WALL) {
        if (wallDist > -std::numeric_limits<f32>::min()) {
            f32 dot = 1.0f - wallNrm.ps_dot(fnrm);
            if (dot > perpendicularity) {
                perpendicularity = std::min(dot, 1.0f);
            }
        }

        updateWall(now_dist, fnrm);
    }
}

/// @addr{0x807C26AC}
void CollisionInfo::transformInfo(CollisionInfo &rhs, const EGG::Matrix34f &mtx,
        const EGG::Vector3f &v) {
    rhs.bbox.min = mtx.ps_multVector33(rhs.bbox.min);
    rhs.bbox.max = mtx.ps_multVector33(rhs.bbox.max);

    EGG::Vector3f min = rhs.bbox.min;

    rhs.bbox.min = min.minimize(rhs.bbox.max);
    rhs.bbox.max = min.maximize(rhs.bbox.max);

    bbox.min = bbox.min.minimize(rhs.bbox.min);
    bbox.max = bbox.max.maximize(rhs.bbox.max);

    if (floorDist < rhs.floorDist) {
        floorDist = rhs.floorDist;
        floorNrm = mtx.ps_multVector33(rhs.floorNrm);
    }

    if (wallDist < rhs.wallDist) {
        wallDist = rhs.wallDist;
        wallNrm = mtx.ps_multVector33(rhs.wallNrm);
    }

    if (movingFloorDist < rhs.floorDist) {
        movingFloorDist = rhs.floorDist;
        roadVelocity = v;
    }

    perpendicularity = std::max(perpendicularity, rhs.perpendicularity);
}

} // namespace Kinoko::Field
