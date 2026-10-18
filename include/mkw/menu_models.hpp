#pragma once
#include "mkw/animation.hpp"
#include "mkw/menu_transition.hpp"
#include "mkw/asset_loader.hpp"
#include <memory>
#include <vector>

namespace mkw {
// QA comparison switch (menu-cpu-skin.txt): forces the CPU skinning path.
inline bool menuRigidModels=true;
// Original menu driver, machine and background models. Loads run on the
// AssetLoader; a small LRU cache keeps recently shown and prefetched models.
// A model on screen stays visible until its replacement is resident, so
// browsing never waits on the SD card. begin() starts a frame and draw()
// sits between the background and foreground UI passes.
class MenuModels {
public:
    MenuModels();
    ~MenuModels();
    // Non-blocking: queues loads for the models this state shows.
    void request(const MenuState&, AssetPriority);
    bool ready(const MenuState&) const;
    // Publishes finished loads, chooses the displayed models for this
    // visual state and evicts least-recently-used cache entries.
    void update(Renderer&, const MenuState& visual);
    // Synchronous load of everything the state shows (QA captures).
    void prepare(Renderer&, const MenuState&);
    void advance(unsigned ticks){backgroundFrame_+=ticks;driverFrame_+=ticks;kartTicks_+=ticks;}
    // The menu camera and its stereo: Mario Kart 7's garage camera, whose
    // subject is the selection models.
    void begin(Renderer&);
    // Stereo disparity of the backdrop pane in the frame begin() started: at
    // the garage's back wall, behind the models.
    static float backdropParallax(const Renderer&);
    void draw(Renderer&, const MenuState&, const UiTransitionView& = {});
    void reset(Renderer&);
    bool hasDriver() const { return shownDriver_ >= 0; }
    struct Entry;
private:
    Entry* find(int kind,int id);
    const Entry* find(int kind,int id) const;
    Entry& obtain(int kind,int id);
    void show(Renderer&,int kind,int id);
    void turnKart(const MenuState&, const UiTransitionView&);
    std::vector<std::unique_ptr<Entry>> entries_;
    int shownDriver_=-1,shownBackground_=-1,shownMachine_=-1;
    int driverClip_=-1,driverFace_=-1,backgroundClip_=-1;
    // The character page's OK press puts its driver model in state 1, sel_gut
    // (CharacterSelect front handler PAL 8083dfa8 -> fn_1_31F7C4).
    bool driverConfirm_=false;
    unsigned backgroundFrame_=0,driverFrame_=0;
    // Menu kart turntable: KartModelMgr's frame count (+0x50, 0..719) and the
    // shown kart's rotation (+0x28); see turnKart().
    unsigned kartTicks_=0,kartCount_=0;
    float kartRotation_=0;
    int kartShown_=-1;
    bool kartVehiclePage_=false;
    std::uint32_t clock_=1;
};
}
