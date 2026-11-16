#pragma once
#include "mkw/asset_loader.hpp"
#include <memory>

namespace mkw {
class Renderer;
class MenuState;
struct UiTransitionView;
// The Settings page's upper screen: a live sample of the highlighted option,
// drawn with the option's current value from the same files and renderer
// paths the race uses. Owned by MenuModels, which forwards its calls: loads
// run on the AssetLoader while the page is open (the frame and labels show
// at once, never waiting on the SD card) and everything is retired when the
// page closes. The window is all the upper screen shows below the header.
class SettingsPreview {
public:
 SettingsPreview();
 ~SettingsPreview();
 void request(const MenuState&,AssetPriority);
 // Publishes finished loads; off the Settings page, retires everything.
 void update(Renderer&,const MenuState&);
 // Starts the frame (MenuModels::begin's projection and lighting already
 // set): the sample's camera, and the Bloom row's request for its window.
 void begin(Renderer&,const MenuState&);
 // Synchronous load (QA snapshots).
 void prepare(Renderer&,const MenuState&);
 // Between the background and foreground UI passes on the upper screen.
 void draw(Renderer&,const MenuState&,const UiTransitionView&);
 void reset(Renderer&);
 // The sample window on the upper screen, in pixels.
 static constexpr float X=52,Y=50,Width=296,Height=146;
private:
 struct Impl;
 std::unique_ptr<Impl> impl_;
};
}
