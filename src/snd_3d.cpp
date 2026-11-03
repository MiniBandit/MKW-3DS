// MKW 3D sound model. Retail: Audio::Engine3D::UpdateAmbientParam (USA
// 806f296c) with Sound3DCalc's CalcAngleAndDistance (806f2024),
// CalcPanStereo (806f23b0) and CalcPitch (806f1e74); constants 8089c4d0.
#include <mkw/snd_3d.hpp>
#include <mkw/explosion_audio.hpp>
#include <algorithm>
#include <cmath>

namespace mkw::snd {
namespace {
f32 dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
f32 length(Vec3f a) { return std::sqrt(dot(a, a)); }
Vec3f sub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f cross(Vec3f a, Vec3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3f normalized(Vec3f a) { const f32 l = length(a); return l > 0 ? Vec3f{a.x / l, a.y / l, a.z / l} : a; }
constexpr f32 Pi = 3.14159265f;
// Stereo speaker bands (8089c510-8089c520): -3pi/4, -pi/4, pi/4, 3pi/4.
constexpr f32 RearLeft = -2.356194496f, FrontLeft = -0.785398185f, FrontRight = 0.785398185f, RearRight = 2.356194496f;
// A previous value moves by at most 0.1 per update (8089c52c/8089c530).
f32 limit(f32 value, f32 previous) { return std::clamp(value, previous - 0.1f, previous + 0.1f); }
}

void Listener3D::lookAt(Vec3f eye, Vec3f target, Vec3f up, f32 scale) {
 position = {eye.x + (target.x - eye.x) * scale, eye.y + (target.y - eye.y) * scale, eye.z + (target.z - eye.z) * scale};
 // C_MTXLookAt(position, up, target): listener space looks down -z.
 const Vec3f look = normalized(sub(position, target));
 const Vec3f right = normalized(cross(up, look));
 const Vec3f upv = cross(look, right);
 const Vec3f axes[3] = {right, upv, look};
 for (int r = 0; r < 3; ++r) { m[r][0] = axes[r].x; m[r][1] = axes[r].y; m[r][2] = axes[r].z; m[r][3] = -dot(position, axes[r]); }
}

void Engine3D::angleAndDistance(Vec3f rel, f32 distance, f32 interior, f32& angle, f32& distanceNrm) {
 Vec3f p{};
 if (distance != 0.0f) {
  Vec3f v{rel.x, 0, rel.z};
  const f32 len = length(v);
  if (len > interior) { v.x *= interior / len; v.z *= interior / len; }
  const f32 inside = length(v);
  p = {rel.x * inside / distance, 0, rel.z * inside / distance};
 }
 angle = p.x == 0.0f && p.z == 0.0f ? 0.0f : std::atan2(p.x, -p.z);
 distanceNrm = interior > 0 ? length(p) / interior : 0.0f;
}

f32 Engine3D::panStereo(f32 angle, f32 distanceNrm) const {
 f32 pan;
 if (angle < RearLeft) pan = angle / -(Pi / 4) - 4.0f;           // 0 at -pi .. -1 at -3pi/4
 else if (angle < FrontLeft) pan = -1.0f;
 else if (angle < FrontRight) pan = angle * 4.0f / Pi;            // -1 .. 1 across the front
 else if (angle < RearRight) pan = 1.0f;
 else pan = angle / -(Pi / 4) + 4.0f;                              // 1 at 3pi/4 .. 0 at pi
 return pan * panRange * distanceNrm;
}

f32 Engine3D::doppler(f32 factor, Vec3f rel, Vec3f actorVelocity, Vec3f listenerVelocity) const {
 if (sonicVelocity == 0.0f) return 1.0f;
 const f32 len = length(rel);
 f32 actor, listener;
 if (len > 0.0f) {
  const Vec3f dir{rel.x / len, rel.y / len, rel.z / len};
  actor = -dot(dir, actorVelocity); listener = -dot(dir, listenerVelocity);
 } else { actor = -length(actorVelocity); listener = -length(listenerVelocity); }
 listener *= factor; actor *= factor;
 f32 pitch;
 if (listener > sonicVelocity) pitch = 0.0f;
 else if (actor >= sonicVelocity) pitch = 65535.0f;
 else pitch = (sonicVelocity - listener) / (sonicVelocity - actor);
 return std::min(pitch, 2.0f);
}

Ambient Engine3D::update(const SoundEntry& s, u32 soundId, const Actor3D& actor, const Listener3D& l, Sound3DHistory* h) const {
 // Sound3DEngine::UpdateAmbientParam (8009d180): the sound's 3D flags turn
 // parts off; Doppler needs a factor.
 u32 flags = (s.flags3d & 1) ? 0x1eu : 0x1fu;
 if (s.flags3d & 2) flags &= ~2u;
 if (s.flags3d & 4) flags &= ~4u;
 if (s.flags3d & 8) flags &= ~8u;
 if (s.dopplerFactor == 0) flags &= ~0x10u;
 Ambient a;
 const Vec3f rel{l.m[0][0] * actor.position.x + l.m[0][1] * actor.position.y + l.m[0][2] * actor.position.z + l.m[0][3],
                 l.m[1][0] * actor.position.x + l.m[1][1] * actor.position.y + l.m[1][2] * actor.position.z + l.m[1][3],
                 l.m[2][0] * actor.position.x + l.m[2][1] * actor.position.y + l.m[2][2] * actor.position.z + l.m[2][3]};
 const f32 distance = length(rel);
 // Bounded 3DS object mixer: rank spatial objects by listener distance,
 // independent of authored loudness. Other players retain retail policy.
 if (s.playerId == 2) a.distancePriority = 64 - int(std::min(distance / 128.0f, 64.0f));
 const bool primed = h && h->primed;
 if (flags & 9) {
  f32 volume = 1.0f;
  const f32 full = explosionSound(soundId)?std::max(Distance[actor.userParam & 0xf],ExplosionFullDistance):Distance[actor.userParam & 0xf];
  if (distance > full) {
   const f32 units = (distance - full) / (explosionSound(soundId)?ExplosionUnitDistance:unitDistance), ratio = s.decayRatio * (1.0f / 256.0f);
   if (s.decayCurve == 1) volume = std::pow(ratio, units);
   else if (s.decayCurve == 2) volume = std::max(0.0f, 1.0f - (1.0f - ratio) * units);
  }
  if (flags & 8) a.priority = -int((1.0f - volume) * f32(maxPriorityReduction));
  if (flags & 1) {
   if (primed) volume = limit(volume, h->volume);
   a.volume = (actor.userParam & 0x400) && soundId == 0x11b ? volume : volume * l.volume;
   if (h) h->volume = a.volume;
  }
 } else a.volume = l.volume;
 a.fxSend = l.fxSend;
 if (flags & 6) {
  f32 angle, nrm;
  angleAndDistance(rel, distance, Distance[(actor.userParam >> 4) & 0xf], angle, nrm);
  f32 pan = panStereo(angle, nrm);
  if (swapPan) pan = -pan;
  if (flags & 2) { if (primed) pan = limit(pan, h->pan); a.pan = pan; if (h) h->pan = pan; }
  if (flags & 4) { f32 surround = 0.0f; if (primed) surround = limit(surround, h->surround); a.surroundPan = surround; if (h) h->surround = surround; }
 }
 if ((flags & 0x10) && !(actor.userParam & 0x100))
  a.pitch = doppler(f32(s.dopplerFactor) * 0.03125f, sub(actor.position, l.position), actor.velocity, l.velocity);
 if (h) h->primed = true;
 return a;
}
}
