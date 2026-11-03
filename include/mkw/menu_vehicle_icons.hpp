#pragma once
#include <mkw/menu_ui.hpp>
#include <mkw/asset_loader.hpp>
#include <memory>
namespace mkw {
class Renderer;
// Retail vehicle buttons capture machine-only models with IconParam poses.
// Each driver's twelve immutable models load as one AssetLoader job; the
// current set and one prefetched set stay resident. Nothing here blocks.
class MenuVehicleIcons {
public:
 MenuVehicleIcons();
 ~MenuVehicleIcons();
 // Driver -1 requests nothing. Prefetch requests never displace the shown set.
 void request(int driver,AssetPriority);
 bool ready(int driver)const;
 // Publishes finished loads, selects the set for this state, evicts extras.
 void update(Renderer&,const MenuState&);
 // Synchronous load for QA captures; returns false if any model is missing.
 bool prepare(Renderer&,const MenuState&);
 void reset(Renderer&);
 bool draw(Renderer&,const UiQuad&)const;
 unsigned residentCount()const;
 static int driverFor(const MenuState& state){return state.page!=UiPage::Vehicle?-1:state.selection.driver<24?int(state.selection.driver):state.selection.miiSelected?24+int(state.selection.miiCharacter):-1;}
 struct Impl;
private:
 std::unique_ptr<Impl> impl_;
};
}
