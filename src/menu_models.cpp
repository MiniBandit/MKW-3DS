#include <mkw/display_layout.hpp>
#include "mkw/menu_models.hpp"
#include "mkw/menu_model_params.hpp"
#include "mkw/menu_model_motion.hpp"
#include "mkw/menu_lighting.hpp"
#include "mkw/render_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mkw
{
    namespace
    {
        constexpr float EyeDistance = 1000.f, Fov = 45.f, ModelDepth = EyeDistance + 15.f;
        enum Kind
        {
            Driver,
            Background,
            Machine
        };
        // Resident entries per kind: browsing neighbours stay warm, LINEAR stays bounded.
        constexpr unsigned Capacity[] = {6, 4, 12};
        constexpr std::uint32_t LinearReserve = 3u << 20;
        bool machinePage(UiPage page)
        {
            switch (page)
            {
            case UiPage::Vehicle:
            case UiPage::Drift:
            case UiPage::Cup:
            case UiPage::Course:
            case UiPage::BattleCup:
            case UiPage::BattleStage:
            case UiPage::Ghost:
            case UiPage::Confirm:
                return true;
            default:
                return false;
            }
        }
        int backgroundFor(const MenuState &state)
        {
            switch (state.page)
            {
            case UiPage::Mode:
                return int(std::min(state.cursor, 3u));
            case UiPage::EngineClass:
                return 0;
            case UiPage::VSMode:
                return 2;
            case UiPage::BattleMode:
                return 3;
            case UiPage::Character:
            case UiPage::Vehicle:
            case UiPage::Drift:
            case UiPage::Cup:
            case UiPage::Course:
            case UiPage::BattleCup:
            case UiPage::BattleStage:
            case UiPage::Ghost:
            case UiPage::Confirm:
                return int(state.selection.mode);
            default:
                return -1;
            }
        }
        const char *seatedClip(unsigned driver, unsigned choice)
        {
            // DriverModelAnmCalculator::GetPrefix, original USA REL 0x8084e9f8.
            constexpr unsigned table[] = {0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11};
            const unsigned weight = UiCharacters[driver].weight, vehicle = table[choice] * 3 + weight;
            if (vehicle < 18)
                return "kart-wait";
            if (weight == 0)
                return "sdf_bike-wait";
            if (weight == 1)
                return vehicle == 22 || vehicle == 31 || vehicle == 34 ? "ma_bike-wait" : "mdf_bike-wait";
            return vehicle == 26 || vehicle == 29 || vehicle == 35 ? "lb_bike-wait" : "ldf_bike-wait";
        }
        struct Wanted
        {
            int driver = -1, machine = -1, background = -1;
        };
        Wanted wanted(const MenuState &state)
        {
            Wanted w;
            w.driver = state.page == UiPage::Character ? int(state.characterDriver(state.cursor)) : machinePage(state.page) ? int(state.selection.driver)
                                                                                                                            : -1;
            const unsigned choice = state.page == UiPage::Vehicle ? state.vehicleChoice(state.cursor) : state.selection.vehicle;
            w.machine = machinePage(state.page) && w.driver >= 0 && w.driver < 24 && choice < 12 ? w.driver * 12 + int(choice) : -1;
            if (w.driver >= 24)
                w.driver = -1;
            w.background = backgroundFor(state);
            return w;
        }
        void applyEnvironment(Model &model, const std::vector<uint16_t> &vertices, float yaw)
        {
            if (vertices.empty())
                return;
            const float c = std::cos(yaw), s = std::sin(yaw);
            for (const auto index : vertices)
            {
                auto &vertex = model.vertices[index];
                const auto normal = normalized(vertex.normal);
                vertex.uv1[0] = .5f + .5f * (c * normal.x + s * normal.z);
                vertex.uv1[1] = .5f + .5f * normal.y;
            }
            flushGpuData(model.vertices, model.vertexCount * sizeof(Vertex));
        }
        // Unique vertices of the environment-mapped (reflective) submeshes.
        std::vector<uint16_t> environmentVertices(const Model &model)
        {
            std::vector<bool> seen(model.vertexCount, false);
            std::vector<uint16_t> result;
            for (const auto &range : model.submeshes)
            {
                if (!(range.flags & (1u << 25)) || ((range.flags >> 12) & 3) != 3)
                    continue;
                for (unsigned i = range.first; i < range.first + range.count; ++i)
                {
                    const auto v = model.indices[i];
                    if (!seen[v])
                    {
                        seen[v] = true;
                        result.push_back(v);
                    }
                }
            }
            return result;
        }
        struct ModelJob final : AssetJob
        {
            int kind, id;
            std::string modelPath, animationPath;
            std::unique_ptr<Model> model;
            std::unique_ptr<DriverAnimation> animation;
            std::vector<uint16_t> environment;
            ModelJob(int k, int i) : kind(k), id(i)
            {
                label = k == Driver ? "driver model" : k == Background ? "background model"
                                                                       : "machine model";
                char path[96];
                if (k == Driver)
                {
                    std::snprintf(path, sizeof path, "models/ui_driver_%d_000.gmdl", i);
                    modelPath = path;
                    std::snprintf(path, sizeof path, "models/ui_driver_%d.dskm", i);
                    animationPath = path;
                }
                if (k == Background)
                {
                    std::snprintf(path, sizeof path, "models/ui_back_%d_000.gmdl", i);
                    modelPath = path;
                    std::snprintf(path, sizeof path, "models/ui_back_%d.dskm", i);
                    animationPath = path;
                }
                if (k == Machine)
                {
                    std::snprintf(path, sizeof path, "models/ui_machine_%d_%u_000.gmdl", i / 12, unsigned(i % 12));
                    modelPath = path;
                }
            }
            void run() override
            {
                auto loaded = std::make_unique<Model>();
                if (!loaded->load(modelPath.c_str()))
                    return;
                if (!animationPath.empty())
                {
                    auto bank = std::make_unique<DriverAnimation>();
                    if (bank->load(animationPath.c_str(), *loaded))
                    {
                        // A newly selected driver first shows sel_wait frame zero.
                        if (kind == Driver)
                            bank->apply(*loaded, bank->find("sel_wait"), 0, 0, bank->face("sel_wait"), 0);
                        animation = std::move(bank);
                    }
                }
                // Rigid backgrounds (one bone per triangle) skin and environment-map
                // on the GPU; deforming ones keep the CPU path.
                if (kind == Background && !(menuRigidModels && animation && animation->makeRigid(*loaded)))
                {
                    environment = environmentVertices(*loaded);
                    applyEnvironment(*loaded, environment, 0.f);
                }
                model = std::move(loaded);
            }
        };
    }
    struct MenuModels::Entry
    {
        int kind = -1, id = -1;
        std::shared_ptr<ModelJob> job;
        std::unique_ptr<Model> model;
        std::unique_ptr<DriverAnimation> animation;
        std::vector<uint16_t> environment;
        std::uint32_t used = 0, pinned = 0;
        bool failed = false;
        bool resident() const { return model != nullptr; }
    };
    MenuModels::MenuModels() = default;
    MenuModels::~MenuModels() = default;
    MenuModels::Entry *MenuModels::find(int kind, int id)
    {
        for (auto &e : entries_)
            if (e->kind == kind && e->id == id)
                return e.get();
        return nullptr;
    }
    const MenuModels::Entry *MenuModels::find(int kind, int id) const
    {
        for (const auto &e : entries_)
            if (e->kind == kind && e->id == id)
                return e.get();
        return nullptr;
    }
    MenuModels::Entry &MenuModels::obtain(int kind, int id)
    {
        if (auto *e = find(kind, id))
            return *e;
        auto entry = std::make_unique<Entry>();
        entry->kind = kind;
        entry->id = id;
        entries_.push_back(std::move(entry));
        return *entries_.back();
    }
    void MenuModels::request(const MenuState &state, AssetPriority priority)
    {
        const auto w = wanted(state);
        const std::pair<int, int> items[] = {{Driver, w.driver}, {Background, w.background}, {Machine, w.machine}};
        for (const auto &[kind, id] : items)
        {
            if (id < 0)
                continue;
            auto &e = obtain(kind, id);
            e.used = clock_;
            if (priority != AssetPriority::Prefetch)
                e.pinned = clock_;
            if (e.resident() || e.failed)
                continue;
            if (!e.job || (e.job->done() && e.job->cancelled()))
                e.job = std::make_shared<ModelJob>(kind, id);
            if (!e.job->done())
                AssetLoader::instance().submit(e.job, priority);
        }
    }
    bool MenuModels::ready(const MenuState &state) const
    {
        const auto w = wanted(state);
        const std::pair<int, int> items[] = {{Driver, w.driver}, {Background, w.background}, {Machine, w.machine}};
        for (const auto &[kind, id] : items)
        {
            if (id < 0)
                continue;
            const auto *e = find(kind, id);
            if (!e || (!e->resident() && !e->failed))
                return false;
        }
        return true;
    }
    void MenuModels::show(Renderer &, int kind, int id)
    {
        auto *e = id >= 0 ? find(kind, id) : nullptr;
        if (kind == Driver && shownDriver_ != id)
        {
            shownDriver_ = id;
            driverFrame_ = 0;
            driverClip_ = -1;
            driverFace_ = -1;
            if (e && e->animation)
            {
                e->animation->clearPoseTransition();
                driverClip_ = e->animation->find("sel_wait");
                driverFace_ = e->animation->face("sel_wait");
            }
        }
        if (kind == Background && shownBackground_ != id)
        {
            shownBackground_ = id;
            backgroundFrame_ = 0;
            backgroundClip_ = e && e->animation ? e->animation->find("loop") : -1;
        }
        if (kind == Machine)
            shownMachine_ = id;
    }
    void MenuModels::update(Renderer &renderer, const MenuState &visual)
    {
        for (auto &e : entries_)
            if (e->job && e->job->done())
            {
                auto job = std::move(e->job);
                if (job->model)
                {
                    e->model = std::move(job->model);
                    e->animation = std::move(job->animation);
                    e->environment = std::move(job->environment);
                }
                else if (!job->cancelled())
                    e->failed = true;
            }
        const auto w = wanted(visual);
        // Show the wanted model once resident; meanwhile keep the previous one.
        auto choose = [&](int kind, int id, int shown)
        {
            if (id < 0)
            {
                show(renderer, kind, -1);
                return;
            }
            const auto *e = find(kind, id);
            if (e && e->resident())
                show(renderer, kind, id);
            else if (e && e->failed)
                show(renderer, kind, -1);
            else if (shown >= 0 && !find(kind, shown))
                show(renderer, kind, -1);
        };
        choose(Background, w.background, shownBackground_);
        choose(Machine, w.machine, shownMachine_);
        // The seated driver must be the one on the displayed machine.
        choose(Driver, shownMachine_ >= 0 ? shownMachine_ / 12 : w.driver, shownDriver_);
        if (auto *driver = shownDriver_ >= 0 ? find(Driver, shownDriver_) : nullptr; driver && driver->animation)
        {
            const int clip = driver->animation->find(shownMachine_ >= 0 ? seatedClip(unsigned(shownMachine_ / 12), unsigned(shownMachine_ % 12)) : "sel_wait");
            if (clip != driverClip_)
            {
                driver->animation->clearPoseTransition();
                driverFrame_ = 0;
            }
            driverClip_ = clip;
        }
        for (auto &e : entries_)
        {
            const bool shown = (e->kind == Driver && e->id == shownDriver_) || (e->kind == Background && e->id == shownBackground_) || (e->kind == Machine && e->id == shownMachine_);
            if (shown)
                e->used = clock_;
        }
        // Evict least recently used, never what is shown or pinned this frame.
        auto evictable = [&](const Entry &e)
        {
            const bool shown = (e.kind == Driver && e.id == shownDriver_) || (e.kind == Background && e.id == shownBackground_) || (e.kind == Machine && e.id == shownMachine_);
            return !shown && e.pinned != clock_;
        };
        std::uint32_t retiring = 0;
        for (;;)
        {
            unsigned counts[3]{};
            for (const auto &e : entries_)
                if (e->resident() || e->job)
                    ++counts[e->kind];
            Entry *victim = nullptr;
            const bool low = linearSpaceFree() + retiring < LinearReserve;
            for (auto &e : entries_)
            {
                if (!evictable(*e) || !(e->resident() || e->job))
                    continue;
                if (!low && counts[e->kind] <= Capacity[e->kind])
                    continue;
                if (!victim || e->used < victim->used)
                    victim = e.get();
            }
            if (!victim)
                break;
            if (victim->job)
            {
                victim->job->cancel();
                victim->job.reset();
            }
            if (victim->model)
                retiring += victim->model->vertexCount * sizeof(Vertex);
            renderer.retire(std::move(victim->model));
            victim->animation.reset();
            victim->environment.clear();
        }
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const auto &e)
                                      { return !e->resident() && !e->job && !e->failed && evictable(*e); }),
                       entries_.end());
        ++clock_;
    }
    void MenuModels::prepare(Renderer &renderer, const MenuState &state)
    {
        request(state, AssetPriority::Urgent);
        // QA path: run this state's jobs on the calling thread.
        for (auto &e : entries_)
            if (e->job && !e->job->done())
            {
                e->job->cancel();
                auto job = std::make_shared<ModelJob>(e->kind, e->id);
                job->run();
                if (job->model)
                {
                    e->model = std::move(job->model);
                    e->animation = std::move(job->animation);
                    e->environment = std::move(job->environment);
                }
                else
                    e->failed = true;
                e->job.reset();
            }
        update(renderer, state);
    }
    void MenuModels::reset(Renderer &renderer)
    {
        for (auto &e : entries_)
        {
            if (e->job)
                e->job->cancel();
            renderer.retire(std::move(e->model));
        }
        entries_.clear();
        shownDriver_ = shownBackground_ = shownMachine_ = -1;
        driverClip_ = driverFace_ = backgroundClip_ = -1;
    }
    void MenuModels::begin(Renderer &renderer)
    {
        renderer.setFov(Fov);
        renderer.setCourseLighting(false);
        renderer.setRetailLighting(&MenuLightings[0]);
        // Stereo: Mario Kart 7's garage, the selection models 7 px behind the
        // screen (stereo::Garage). They stand at modelZ -15 (see draw).
        renderer.setStereoSubject(stereo::Garage, ModelDepth);
        renderer.begin({0, 0, EyeDistance}, {0, 0, 0}, {0, 1, 0});
    }
    float MenuModels::backdropParallax(const Renderer &renderer)
    {
        return renderer.stereoParallax(ModelDepth * stereo::GarageBackdropRatio);
    }
    void MenuModels::draw(Renderer &renderer, const MenuState &state, const UiTransitionView &motion)
    {
        if (auto *background = shownBackground_ >= 0 ? find(Background, shownBackground_) : nullptr; background && background->resident())
        {
            // BackModelMgr::Update's original rates (USA808d5f18) continue across
            // selection pages; a page's age is not the model's animation clock.
            constexpr float rates[] = {.5f, .4f, .25f, .5f};
            auto t0 = svcGetSystemTick();
            const bool rigid = background->animation && background->animation->rigid();
            RigidPose pose;
            if (rigid)
            {
                pose.parts = &background->animation->rigidParts();
                pose.palette = background->animation->pose(backgroundClip_, float(backgroundFrame_) * rates[shownBackground_], backgroundFrame_);
            }
            else if (background->animation)
                background->animation->apply(*background->model, backgroundClip_, float(backgroundFrame_) * rates[shownBackground_], backgroundFrame_);
            auto t1 = svcGetSystemTick();
            renderMetrics.skinMs += float((t1 - t0) * 1000.0 / SYSCLOCK_ARM11);
            // Original full 608x480 background render target is displayed in the
            // 608x456 aspect-adjusted pane at BRCTR X80. Z's original BSS table is
            // all zero: its only two REL relocations address the read in Update.
            constexpr float scales[] = {.1f, .14f, .19f, .11f};
            constexpr float xs[] = {0, 0, -9, -1}, ys[] = {-3.4f, -5.1f, -9, -2.5f};
            constexpr float unit = EyeDistance / 15.f;
            C3D_Mtx matrix;
            Mtx_Identity(&matrix);
            Mtx_Translate(&matrix, xs[shownBackground_] * unit, ys[shownBackground_] * unit, 0, true);
            const float scale = scales[shownBackground_] * unit;
            Mtx_Scale(&matrix, scale, scale, scale);
            if (!rigid || pose.palette)
            {
                if (rigid)
                    renderer.setRigidPose(&pose);
                renderer.drawAtScreen(*background->model, matrix, 200.f + 80.f * 400.f / 608.f, 120.f,
                                      100.f / 255.f, 2.f * 400.f / 832.f, display::Height / 240.f);
                renderer.setRigidPose(nullptr);
            }
            renderMetrics.drawMs += float((svcGetSystemTick() - t1) * 1000.0 / SYSCLOCK_ARM11);
        }
        auto *driver = shownDriver_ >= 0 ? find(Driver, shownDriver_) : nullptr;
        if (!driver || !driver->resident())
            return;
        {
            const auto t0 = svcGetSystemTick();
            if (driver->animation)
                driver->animation->apply(*driver->model, driverClip_, float(driverFrame_), driverFrame_, driverFace_, float(driverFrame_));
            renderMetrics.skinMs += float((svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
        }
        auto *machine = shownMachine_ >= 0 ? find(Machine, shownMachine_) : nullptr;
        if (machine && machine->resident())
        {
            const unsigned driverId = unsigned(shownMachine_) / 12, choice = unsigned(shownMachine_) % 12;
            // Original centered 480-high framebuffer is cropped to a 256-square
            // image, then placed by model_set_p. Its AspectAdjust flag reduces
            // horizontal scale to 608/832 before the 608x456 UI reaches the LCD.
            C3D_Mtx matrix;
            Mtx_Identity(&matrix);
            const auto placement = uiMenuModelPlacement(state.page, motion);
            const float x = 200.f + placement.x * 400.f / 608.f;
            const float y = 120.f - placement.y * display::MenuScaleY;
            constexpr unsigned ids[] = {0, 1, 2, 6, 7, 8, 3, 4, 5, 9, 10, 11};
            const unsigned weight = UiCharacters[driverId].weight, vehicle = ids[choice] * 3 + weight;
            const float scale = vehicle == 5 ? 1.03f : weight == 0 ? 1.3f
                                                   : weight == 1   ? 1.2f
                                                                   : 1.1f;
            // EyeZ1000 plus modelZ-15 is the native eyeZ15/modelZ-1000 pair.
            Mtx_Translate(&matrix, 0.f, -185.f, -15.f, true);
            // Retail Update builds Euler Y=(90+rotation)*2*pi/720.
            Mtx_RotateY(&matrix, 3.14159265359f / 4.f, true);
            Mtx_Scale(&matrix, scale, scale, scale);
            renderer.drawAtScreen(*machine->model, matrix, x, y, 1.f, 2.f * 400.f / 832.f * placement.scale, 2.f * display::MenuScaleY * placement.scale);
            const auto &seat = MenuSeats[driverId][choice];
            Mtx_Translate(&matrix, 0, seat.y, seat.z, true);
            renderer.drawAtScreen(*driver->model, matrix, x, y, 1.f, 2.f * 400.f / 832.f * placement.scale, 2.f * display::MenuScaleY * placement.scale);
        }
        else if (state.page == UiPage::Character)
        {
            C3D_Mtx matrix;
            Mtx_Identity(&matrix);
            Mtx_Translate(&matrix, 0.f, -150.f, -15.f, true);
            Mtx_Scale(&matrix, 1.6f, 1.6f, 1.6f);
            const auto placement = uiMenuModelPlacement(state.page, motion);
            renderer.drawAtScreen(*driver->model, matrix, 200.f + placement.x * 400.f / 608.f,
                                  120.f - placement.y * display::MenuScaleY, 1.f,
                                  2.f * 400.f / 832.f * placement.scale, 2.f * display::MenuScaleY * placement.scale);
        }
    }
}
