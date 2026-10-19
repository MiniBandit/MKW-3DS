#include <game/field/obj/ObjectKarehayama.hh>
#pragma once
#include <mkw/race_effects.hpp>
#include <mkw/kart_smoke_effects.hpp>
#include <mkw/kart_effect_lod.hpp>
#include <game/field/ObjectDirector.hh>
#include <game/field/obj/ObjectPenguin.hh>
#include <game/field/obj/ObjectChoropu.hh>
#include <game/field/obj/ObjectCrab.hh>
#include <game/field/obj/ObjectRock.hh>
#include <game/field/obj/ObjectItemboxLine.hh>
#include <game/field/obj/ObjectWoodbox.hh>
#include <game/field/obj/ObjectLeafEffect.hh>
#include <game/field/obj/ObjectChimney.hh>
#include <game/kart/KartObjectManager.hh>
#include <game/kart/KartObject.hh>
#include <game/system/RaceConfig.hh>
#include <game/field/obj/ObjectPropeller.hh>
#include <game/field/obj/ObjectVolcanoBall.hh>
#include <game/field/obj/ObjectVolcanoBallLauncher.hh>
#include <game/field/obj/ObjectSanbo.hh>
#include <game/field/obj/ObjectCannon.hh>
#include <game/field/obj/ObjectHanachan.hh>
#include <game/field/obj/ObjectFactoryEffect.hh>
#include <mkw/hanachan_joint_assets.hpp>
#include <game/field/obj/ObjectKoopaFigure.hh>
#include <mkw/cannon_effect_assets.hpp>
#include <cstring>
#include <array>

namespace mkw {
// Course-owned effects use group0, as the original EGG::Effect constructors.
// Each owner's two handles survive its draw culling and follow its live pose.
class CreatureEffects {
public:
    static constexpr unsigned FirstSlot=KartLandingEffects::FirstSlot+12*3;
    static constexpr unsigned MaxPenguins=32,SlotsPerPenguin=2;
    static constexpr unsigned CrabFirstSlot=FirstSlot+MaxPenguins*SlotsPerPenguin,MaxCrabs=32;
    static constexpr unsigned PokeyFirstSlot=CrabFirstSlot+MaxCrabs*3,MaxPokeys=32;
    static constexpr unsigned FactoryFirstSlot=PokeyFirstSlot+MaxPokeys,MaxFactoryEffects=32;
    static constexpr unsigned MoleFirstSlot=FactoryFirstSlot+MaxFactoryEffects,MaxMoles=16;
    static_assert(MoleFirstSlot+MaxMoles*2<=RaceEffects::SlotCount);
    static constexpr unsigned PropellerFirstSlot=921,MaxPropellers=8;
    static_assert(PropellerFirstSlot+MaxPropellers*3<=RaceEffects::SlotCount);
    static constexpr unsigned VolcanoFirstSlot=PropellerFirstSlot+MaxPropellers*3,MaxVolcanoBalls=128;
    static_assert(VolcanoFirstSlot+MaxVolcanoBalls*2<=RaceEffects::SlotCount);
    static constexpr unsigned LeafFirstSlot=KartBrakeEffects::FirstSlot+KartBrakeEffects::MaxRacers,MaxLeaves=50;
    static_assert(LeafFirstSlot+MaxLeaves*2<=RaceEffects::SlotCount);
    static constexpr unsigned ChimneyFirstSlot=LeafFirstSlot+MaxLeaves*2,MaxChimneys=18,ChimneySlots=13;
    static_assert(ChimneyFirstSlot+MaxChimneys*ChimneySlots<=RaceEffects::SlotCount);
    bool bind(RaceEffects& effects){
        const char* chimneyNames[]={"rk_truckChimSmkWCalc","rk_truckChimSmkW_A","rk_truckChimSmkW_B","rk_truckChimSmkW1T","rk_truckChimSmkW1TS","W_rk_truckChimSmkW_A","W_rk_truckChimSmkW_B","W_rk_truckChimSmkW1T","W_rk_truckChimSmkW1TS"};
        for(unsigned i=0;i<9;++i)chimneySpecs_[i]=effects.find(chimneyNames[i]);
        pileSpecs_[0]=effects.find("rk_karehayamaS");pileSpecs_[1]=effects.find("rk_karehayama0S");
        leafSpecs_[0]=effects.find("rk_leaf_effect");leafSpecs_[1]=effects.find("rk_leaf_effectDash");
        volcanoSpecs_[0]=effects.find("rk_volcTail");volcanoSpecs_[1]=effects.find("rk_volcFire_L");
        volcanoSpecs_[2]=effects.find("rk_volcErupt");volcanoSpecs_[3]=effects.find("rk_volcBrk");
        propellerSpec_=effects.find("rk_epropeller");
        rockBreak_=effects.find("rk_DKrockGC");
        blockBreak_=effects.find("rk_block");
        woodBreak_=effects.find("rk_woodbox");
        moleSpecs_[0]=effects.find("rk_choropuMoko1");moleSpecs_[1]=effects.find("rk_choropuMoko0");
        spinSmall_=effects.find("rk_pgnSpin_s");
        spinMedium_=effects.find("rk_pgnSpin_m_l");
        slider_=effects.find("rk_pgnSlider_s");
        crabSpecs_[0]=effects.find("rk_crabHide");crabSpecs_[1]=effects.find("rk_crabPocha");
        crabSpecs_[2]=effects.find("rk_crab");
        cannonSmoke_=effects.find("rk_dkCannon");cannonSnow_=effects.find("rk_dkCannonSnw");
        koopaBreath_=effects.find("rk_koopaBallS");
        pokeySand_=effects.find("rk_sanbo");
        starRing_=effects.find("rk_StarRing");
        hanachanDust_=effects.find("rk_hanaSmk");
        factorySpecs_[0]=effects.find("rk_steam");factorySpecs_[1]=effects.find("rk_alarm");
        return effects.ready; // Course-specific required roots are validated by the loader.
    }
    void reset(){piles_={};pileCount_=0;chimneys_={};chimneyCount_=0;leaves_={};leafCount_=0;wood_={};woodCount_=0;blocks_={};blockCount_=0;volcano_={};volcanoCount_=0;propellers_={};propellerCount_=0;rocks_={};rockCount_=0;moles_={};moleCount_=0;factory_={};factoryCount_=0;hanachan_={};hanachanCount_=0;count_=0;crabCount_=0;linked_=false;owners_={};crabs_={};cannon_=nullptr;cannonSerial_=0;statues_={};statueCount_=0;pokeys_={};pokeyCount_=0;}
    void step(RaceEffects& effects){
        namespace K=Kinoko;
        if(!linked_){
            auto* director=K::Field::ObjectDirector::Instance();if(!director)return;
            for(auto* object:director->objects()){
                const auto id=object->id();
                if((unsigned(id)==0x14c||unsigned(id)==0x2c0)&&chimneyCount_<chimneys_.size()){
                    auto& owner=chimneys_[chimneyCount_++];owner.object=static_cast<K::Field::ObjectChimney*>(object);owner.clip=clipSettings(*object);
                    // Original OnStart creates ordinary1 / W1/2 before the
                    // first Update can set DisableDraw on the live handle.
                    const auto& c=*owner.object;
                    if((c.wide()&&c.variant()!=0)||(!c.wide()&&c.variant()==1)){
                        EffectFrame at;const auto p=c.pos();at.origin={p.x,p.y,p.z};
                        const int root=c.wide()?(c.variant()==1?5:6):0;
                        effects.create(ChimneyFirstSlot+(chimneyCount_-1)*ChimneySlots,chimneySpecs_[root],at,0);
                        owner.started=c.wide();
                    }
                }
                if(unsigned(id)==0x176&&pileCount_<piles_.size())piles_[pileCount_++].object=static_cast<K::Field::ObjectKarehayama*>(object);
                if(unsigned(id)==0x175&&leafCount_<leaves_.size()){
                    auto& owner=leaves_[leafCount_++];owner.object=static_cast<K::Field::ObjectLeafEffect*>(object);owner.clip=clipSettings(*object);
                }
                // Both launcher and children share the id/GOBJ; only the balls
                // construct collision (the launcher overrides createCollision with no-op).
                if(id==K::Field::ObjectId::VolcanoBall&&
                        static_cast<const K::Field::ObjectCollidable*>(object)->collision()&&volcanoCount_<volcano_.size())
                    volcano_[volcanoCount_++].object=static_cast<const K::Field::ObjectVolcanoBall*>(object);
                if(id==K::Field::ObjectId::Propeller&&propellerCount_<propellers_.size())
                    propellers_[propellerCount_++]=static_cast<const K::Field::ObjectPropeller*>(object);
                if(id==K::Field::ObjectId::ItemboxLine&&blockCount_<blocks_.size()&&
                        static_cast<const K::Field::ObjectCollidable*>(object)->collision()&&
                        !std::strcmp(object->getResources(),"Block"))
                    blocks_[blockCount_++].object=static_cast<const K::Field::ObjectItemboxBlock*>(object);
                if((id==K::Field::ObjectId::Woodbox||id==K::Field::ObjectId::W_Woodbox)&&
                        woodCount_<wood_.size()&&static_cast<const K::Field::ObjectCollidable*>(object)->collision())
                    wood_[woodCount_++].object=static_cast<const K::Field::ObjectWoodbox*>(object);
                if(id==K::Field::ObjectId::DKRockGC&&rockCount_<rocks_.size())
                    rocks_[rockCount_++].object=static_cast<const K::Field::ObjectRock*>(object);
                if(id==K::Field::ObjectId::Choropu&&object->mapObj()&&
                        !std::strcmp(object->getName(),"choropu")&&moleCount_<moles_.size())
                    moles_[moleCount_++]=static_cast<const K::Field::ObjectChoropu*>(object);
                const auto* flow=director->flowTable().set(director->flowTable().slot(id));
                if(flow&&(!std::strcmp(flow->name,"Steam")||!std::strcmp(flow->name,"Alarm"))&&factoryCount_<factory_.size())
                    factory_[factoryCount_++]=static_cast<const K::Field::ObjectFactoryEffect*>(object);
                if(flow&&!std::strcmp(flow->name,"koopaFigure")&&statueCount_<statues_.size())
                    statues_[statueCount_++].object=static_cast<const K::Field::ObjectKoopaFigure*>(object);
                if(flow&&!cannon_&&K::Field::ObjectCannon::matches(flow->name))
                    cannon_=static_cast<const K::Field::ObjectCannon*>(object);
                // Spawned head/body pieces share the Hanachan id, but only the
                // placed parent owns the part array. Never cast a child to its manager.
                if(id==K::Field::ObjectId::Hanachan&&
                        !static_cast<const K::Field::ObjectCollidable*>(object)->collision()&&hanachanCount_<hanachan_.size())
                    hanachan_[hanachanCount_++].object=static_cast<const K::Field::ObjectHanachan*>(object);
                if(id==K::Field::ObjectId::Sanbo&&pokeyCount_<pokeys_.size())
                    pokeys_[pokeyCount_++]=static_cast<const K::Field::ObjectSanbo*>(object);
                if(id==K::Field::ObjectId::Crab){
                    if(crabCount_<crabs_.size())crabs_[crabCount_++].object=static_cast<const K::Field::ObjectCrab*>(object);
                    continue;
                }
                if(id!=K::Field::ObjectId::PenguinS&&id!=K::Field::ObjectId::PenguinM&&id!=K::Field::ObjectId::PenguinL)continue;
                if(count_==owners_.size())continue; // Original race has17penguins.
                owners_[count_++].object=static_cast<const K::Field::ObjectPenguin*>(object);
            }
            linked_=true;
        }
        for(unsigned i=0;i<chimneyCount_;++i){
            auto& owner=chimneys_[i];const auto& chimney=*owner.object;
            const unsigned base=ChimneyFirstSlot+i*ChimneySlots;
            EffectFrame at;const auto p=chimney.pos();at.origin={p.x,p.y,p.z};
            const unsigned variant=chimney.variant();
            const int root=chimney.wide()?(variant==1?5:6):(variant==2?1:variant==4?2:0);
            const bool oneStart=chimney.wide()||variant==2||variant==4;
            const bool hidden=chimney.effectSuppressed(),entered=hidden&&!owner.suppressed,left=!hidden&&owner.suppressed;
            // Original W and timed smoke only disable drawing: emission and
            // particle age continue. The flag call precedes a possible Create.
            if(oneStart&&(entered||left))effects.setDisabled(base,false,hidden);
            if(!oneStart&&left)effects.setDisabled(base,false,false);
            const bool create=oneStart||!hidden;
            if(chimney.emitting()&&create&&(!oneStart||!owner.started)){
                effects.create(base,chimneySpecs_[root],at,0);owner.started=true;
            }
            if(oneStart||!hidden)effects.move(base,at);
            if(!chimney.wide()&&((variant==1&&hidden&&chimney.elapsed()==2)||(variant==3&&entered)))
                effects.setDisabled(base,true,true);
            effects.stopParticleTrack(base,1,chimney.pauseTrack());
            owner.suppressed=hidden;
            for(unsigned player=0;player<12;++player){
                const unsigned slot=base+1+player;const auto serial=chimney.hitSerial(player);
                if(serial==owner.serial[player]&&!effects.alive(slot))continue;
                const auto* kart=K::Kart::KartObjectManager::Instance()->object(player);
                EffectFrame frame;const auto pos=kart->pos();frame.origin={pos.x,pos.y,pos.z};
                if(!chimney.wide()){
                    const auto& pose=kart->pose();auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
                    frame.x=vec(pose.base(0));frame.y=vec(pose.base(1));frame.z=vec(pose.base(2));
                }
                if(serial!=owner.serial[player]){
                    owner.serial[player]=serial;
                    const bool local=K::System::RaceConfig::Instance()->raceScenario().players[player].type==K::System::RaceConfig::Player::Type::Local;
                    effects.create(slot,chimneySpecs_[chimney.wide()?7:local?3:4],frame,0);
                }
                effects.move(slot,frame);
            }
        }
        for(unsigned i=0;i<leafCount_;++i)for(unsigned kind=0;kind<2;++kind){
            auto& owner=leaves_[i];const auto& leaf=*owner.object;
            const unsigned slot=LeafFirstSlot+2*i+kind;
            const unsigned serial=leaf.effectSerial(kind);
            if(owner.serial[kind]==serial&&!effects.alive(slot))continue;
            const unsigned player=leaf.effectPlayer(kind);
            const auto* kart=K::Kart::KartObjectManager::Instance()->object(player);
            EffectFrame frame;const auto p=kart->pos();frame.origin={p.x,p.y,p.z};
            if(owner.serial[kind]!=leaf.effectSerial(kind)){
                owner.serial[kind]=leaf.effectSerial(kind);
                // Original Create leaves an already-running instance intact.
                effects.create(slot,leafSpecs_[kind],frame,0);
            }
            effects.move(slot,frame);
        }
        for(unsigned i=0;i<volcanoCount_;++i){
            auto& owner=volcano_[i];const auto& ball=*owner.object;
            const unsigned state=ball.stateId(),slot=VolcanoFirstSlot+2*i;
            EffectFrame frame;const auto& pos=ball.pos();frame.origin={pos.x,pos.y,pos.z};frame.scale=ball.scale().x;
            if(state==1&&owner.state!=1){auto eruption=frame;eruption.scale=1;effects.burst(volcanoSpecs_[2],eruption);}
            if(state==2&&owner.state!=2)effects.burst(volcanoSpecs_[3],frame);
            if(state==1){effects.create(slot,volcanoSpecs_[0],frame,0);effects.move(slot,frame);}else effects.fade(slot);
            if(ball.burningEffect()){effects.create(slot+1,volcanoSpecs_[1],frame,0);effects.move(slot+1,frame);}else effects.fade(slot+1);
            owner.state=state;
        }
        for(unsigned i=0;i<propellerCount_;++i)for(unsigned blade=0;blade<3;++blade){
            const auto pose=propellers_[i]->effectPose(blade);
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(pose.base(3));
            frame.x=vec(pose.base(0));frame.y=vec(pose.base(1));frame.z=vec(pose.base(2));
            const unsigned slot=PropellerFirstSlot+i*3+blade;
            effects.create(slot,propellerSpec_,frame,0);effects.move(slot,frame);
        }
        for(unsigned i=0;i<blockCount_;++i){
            auto& owner=blocks_[i];const auto& block=*owner.object;
            if(owner.serial==block.breakCount())continue;
            owner.serial=block.breakCount();
            const auto pos=block.breakPosition();
            EffectFrame frame;frame.origin={pos.x,pos.y,pos.z};
            // Retail sets only world position on its group0 EGG::Effect.
            effects.burst(blockBreak_,frame);
        }
        for(unsigned i=0;i<pileCount_;++i){
            auto& owner=piles_[i];const auto& pile=*owner.object;
            const unsigned serials[]={pile.softSerial(),pile.hardSerial()};
            for(unsigned kind=0;kind<2;++kind)if(owner.serial[kind]!=serials[kind]){
                owner.serial[kind]=serials[kind];const auto& m=pile.transform();const auto scale=pile.scale();
                EffectFrame frame;frame.origin={pile.pos().x,pile.pos().y,pile.pos().z};
                frame.x={m[0,0]*scale.x,m[1,0]*scale.x,m[2,0]*scale.x};
                frame.y={m[0,1]*scale.y,m[1,1]*scale.y,m[2,1]*scale.y};
                frame.z={m[0,2]*scale.z,m[1,2]*scale.z,m[2,2]*scale.z};
                if(!kind)effects.burst(pileSpecs_[kind],frame,pile.scatterVelocity());
                else effects.burst(pileSpecs_[kind],frame);
            }
        }
        for(unsigned i=0;i<woodCount_;++i){
            auto& owner=wood_[i];const auto& box=*owner.object;
            if(owner.serial==box.breakSerial())continue;
            owner.serial=box.breakSerial();
            const auto pos=box.breakPosition(),scale=box.scale();
            EffectFrame frame;frame.origin={pos.x,pos.y,pos.z};
            frame.x={scale.x,0,0};frame.y={0,scale.y,0};frame.z={0,0,scale.z};
            effects.burst(woodBreak_,frame);
        }
        for(unsigned i=0;i<rockCount_;++i){
            auto& owner=rocks_[i];const auto& rock=*owner.object;
            if(owner.serial==rock.breakSerial())continue;
            owner.serial=rock.breakSerial();
            const auto& pose=rock.breakPose();
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(pose.base(3));frame.scale=rock.scale().x;
            frame.x=vec(pose.base(0));frame.y=vec(pose.base(1));frame.z=vec(pose.base(2));
            effects.burst(rockBreak_,frame);
        }
        for(unsigned i=0;i<moleCount_;++i){
            const auto& mole=*moles_[i];
            if(!mole.diggingEffect()){
                effects.fade(MoleFirstSlot+2*i);effects.fade(MoleFirstSlot+2*i+1);continue;
            }
            const auto& pose=mole.diggingPose();
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(pose.base(3));
            frame.x=vec(pose.base(0));frame.y=vec(pose.base(1));frame.z=vec(pose.base(2));
            for(unsigned j=0;j<2;++j){
                const unsigned slot=MoleFirstSlot+2*i+j;
                effects.create(slot,moleSpecs_[j],frame,0);effects.move(slot,frame);
                if(j==1)effects.setParticleLife(slot,mole.diggingLife());
            }
        }
        for(unsigned i=0;i<factoryCount_;++i){
            const auto& object=*factory_[i];
            const auto& pose=object.transform();const auto& scale=object.scale();
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(object.pos());
            frame.x=vec(pose.base(0))*scale.x;frame.y=vec(pose.base(1))*scale.y;frame.z=vec(pose.base(2))*scale.z;
            const unsigned slot=FactoryFirstSlot+i;
            if(object.emitting())effects.create(slot,factorySpecs_[object.steam()?0:1],frame,0);
            else effects.fade(slot);
            effects.move(slot,frame);
        }
        for(unsigned i=0;i<hanachanCount_;++i){
            auto& owner=hanachan_[i];
            for(unsigned j=0;j<owner.object->partCount()&&j<owner.serial.size();++j){
                const auto* part=static_cast<const K::Field::ObjectHanachanPart*>(owner.object->part(j));
                if(!part||owner.serial[j]==part->landingSerial())continue;
                owner.serial[j]=part->landingSerial();
                const auto& pose=part->transform();const auto& scale=part->scale();
                auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
                EffectFrame frame;frame.origin=vec(part->pos()+part->hitOffset());
                frame.x=vec(pose.base(0))*scale.x;frame.y=vec(pose.base(1))*scale.y;
                frame.z=vec(pose.base(2))*scale.z;
                // USA806bc88c: authored skl_root world matrix, not object origin.
                float joint[12];
                const unsigned clip=j? (owner.object->bodyWaiting(j-1)?4:3):owner.object->headAnimation();
                sampleHanachanRoot(clip,owner.object->animationFrame(j),joint);
                const Vec3 x=frame.x,y=frame.y,z=frame.z;
                frame.origin=frame.origin+x*joint[3]+y*joint[7]+z*joint[11];
                frame.x=x*joint[0]+y*joint[4]+z*joint[8];
                frame.y=x*joint[1]+y*joint[5]+z*joint[9];
                frame.z=x*joint[2]+y*joint[6]+z*joint[10];
                effects.burst(hanachanDust_,frame);
            }
        }
        for(unsigned i=0;i<pokeyCount_;++i){
            const auto& pokey=*pokeys_[i];const auto& p=pokey.pos();
            EffectFrame frame;frame.origin={p.x,p.y,p.z};
            const unsigned slot=PokeyFirstSlot+i;
            // USA807659cc: normal keeps the sand emitter alive; scattering
            // retires emission while existing particles follow the moving parent.
            if(!pokey.reviving()&&!pokey.flinching())effects.create(slot,pokeySand_,frame,0);
            if(pokey.scattered())effects.followFade(slot);
            effects.move(slot,frame);
        }
        for(unsigned i=0;i<statueCount_;++i){
            auto& owner=statues_[i];const auto& statue=*owner.object;
            if(owner.serial==statue.breathSerial())continue;
            owner.serial=statue.breathSerial();
            const auto& pose=statue.transform();const auto& scale=statue.scale();
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(statue.pos());
            frame.x=vec(pose.base(0))*scale.x;frame.y=vec(pose.base(1))*scale.y;
            frame.z=vec(pose.base(2))*scale.z;
            // USA8075f1c8 selects the S resource for one screen. USA8075f438
            // starts the authored burst at the statue matrix when vomiting begins.
            effects.burst(koopaBreath_,frame);
        }
        if(cannon_&&cannonSerial_!=cannon_->launchSerial()){
            cannonSerial_=cannon_->launchSerial();
            const auto& p=cannon_->pos();
            EffectFrame frame;frame.origin={p.x,p.y,p.z};
            // USA806bb738: smoke has only a position; no cannon rotation/scale.
            if(cannon_->starRing()){
                const auto& pose=cannon_->transform();const auto& scale=cannon_->scale();
                auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
                frame.x=vec(pose.base(0))*scale.x;frame.y=vec(pose.base(1))*scale.y;
                frame.z=vec(pose.base(2))*scale.z;
                effects.burst(starRing_,frame);
            }else effects.burst(cannonSmoke_,frame);
            if(!std::strcmp(cannon_->getName(),"DonkyCannon_wii")){
                // USA806bb778: snow uses the animated taru joint's full world matrix.
                const auto& pose=cannon_->transform();const auto& scale=cannon_->scale();
                auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
                const Vec3 x=vec(pose.base(0))*scale.x,y=vec(pose.base(1))*scale.y,z=vec(pose.base(2))*scale.z;
                const float* j=CannonSnowJoint;
                frame.origin=frame.origin+x*j[3]+y*j[7]+z*j[11];
                frame.x=x*j[0]+y*j[4]+z*j[8];
                frame.y=x*j[1]+y*j[5]+z*j[9];
                frame.z=x*j[2]+y*j[6]+z*j[10];
                effects.burst(cannonSnow_,frame);
            }
        }
        for(unsigned i=0;i<count_;++i){
            auto& owner=owners_[i];const auto& p=*owner.object;
            const auto& pose=p.transform();
            auto vec=[](const K::EGG::Vector3f& v){return Vec3{v.x,v.y,v.z};};
            EffectFrame frame;frame.origin=vec(p.pos());frame.x=vec(pose.base(0));
            frame.y=vec(pose.base(1));frame.z=vec(pose.base(2));
            frame.scale=p.effectScale(); // Source large-penguin scalar2.5.
            const bool small=p.id()==K::Field::ObjectId::PenguinS;
            const bool active[2]={p.spinning(),small&&p.state()==3};
            const int spec[2]={small?spinSmall_:spinMedium_,slider_};
            for(unsigned n=0;n<2;++n){
                const unsigned slot=FirstSlot+i*SlotsPerPenguin+n;
                if(active[n]&&!owner.active[n])effects.create(slot,spec[n],frame,0);
                else if(!active[n]&&owner.active[n])effects.fade(slot);
                effects.move(slot,frame);owner.active[n]=active[n];
            }
        }
        for(unsigned i=0;i<crabCount_;++i){
            auto& owner=crabs_[i];const auto& crab=*owner.object;
            for(unsigned n=0;n<3;++n){
                const auto& position=crab.effectPosition(n);
                EffectFrame frame;frame.origin={position.x,position.y,position.z};frame.scale=crab.scale().x;
                const unsigned slot=CrabFirstSlot+i*3+n;
                const auto serial=crab.effectSerial(n);
                if(owner.serial[n]!=serial){
                    effects.create(slot,crabSpecs_[n],frame,0);
                    owner.serial[n]=serial;
                    if(n==2)owner.emergeTicks=20;
                }
                effects.move(slot,frame);
                // Retail's first two effects use authored lifetimes; the
                // resurfacing handle explicitly fades after20 simulation ticks.
                if(n==2&&owner.emergeTicks&&!--owner.emergeTicks)effects.fade(slot);
            }
        }
    }
    // ClipInfoMgr runs after cameras. Object callbacks on the next physics
    // step read these flags. A 3DS race currently has one top-screen camera;
    // the lower map is not another world camera. Unused/unknown cameras never
    // justify suppression. Stereo eyes share the same original screen entry.
    void updateClip(const EffectClipCamera* cameras,const bool* forwards,unsigned count){
        const bool valid=cameras&&forwards&&count&&count<=4;
        bool allForward=valid;for(unsigned i=0;valid&&i<count;++i)allForward&=forwards[i];
        auto hidden=[&](const auto& owner){
            if(!allForward)return false;
            const auto p=owner.object->pos();
            for(unsigned i=0;i<count;++i)if(!cameras[i].objectCulled({p.x,p.y,p.z},owner.clip.radius,owner.clip.far,owner.clip.horizontal))return false;
            return true;
        };
        for(unsigned i=0;i<leafCount_;++i)leaves_[i].object->setEffectSuppressed(hidden(leaves_[i]));
        for(unsigned i=0;i<chimneyCount_;++i)chimneys_[i].object->setEffectSuppressed(hidden(chimneys_[i]));
    }
private:
    struct ClipSettings {float radius=0,far=0;bool horizontal=false;};
    static ClipSettings clipSettings(const Kinoko::Field::ObjectBase& object){
        const auto& flow=Kinoko::Field::ObjectDirector::Instance()->flowTable();const auto* entry=flow.set(flow.slot(object.id()));
        if(!entry)return {0,INFINITY,false};
        auto field=[&](unsigned n){return float(static_cast<int16_t>((unsigned(entry->_62[n])<<8)|entry->_62[n+1]));};
        return {field(4),10.f*field(6),field(2)==2};
    }
    struct ChimneyOwner {Kinoko::Field::ObjectChimney* object=nullptr;std::array<unsigned,12> serial{};bool started=false,suppressed=false;ClipSettings clip;};
    std::array<ChimneyOwner,MaxChimneys> chimneys_{};unsigned chimneyCount_=0;
    std::array<int,9> chimneySpecs_{};

    struct PileOwner {const Kinoko::Field::ObjectKarehayama* object=nullptr;std::array<unsigned,2> serial{};};
    std::array<PileOwner,64> piles_{};unsigned pileCount_=0;int pileSpecs_[2]{-1,-1};
    struct LeafOwner {Kinoko::Field::ObjectLeafEffect* object=nullptr;std::array<unsigned,2> serial{};ClipSettings clip;};
    std::array<LeafOwner,MaxLeaves> leaves_{};unsigned leafCount_=0;
    std::array<int,2> leafSpecs_{};

    struct VolcanoOwner {const Kinoko::Field::ObjectVolcanoBall* object=nullptr;unsigned state=0;};
    std::array<VolcanoOwner,MaxVolcanoBalls> volcano_{};
    unsigned volcanoCount_=0;int volcanoSpecs_[4]{-1,-1,-1,-1};
    std::array<const Kinoko::Field::ObjectPropeller*,MaxPropellers> propellers_{};
    unsigned propellerCount_=0;int propellerSpec_=-1;
    std::array<const Kinoko::Field::ObjectChoropu*,MaxMoles> moles_{};
    struct WoodOwner {const Kinoko::Field::ObjectWoodbox* object=nullptr;uint32_t serial=0;};
    std::array<WoodOwner,64> wood_{};
    unsigned woodCount_=0;int woodBreak_=-1;
    struct BlockOwner {const Kinoko::Field::ObjectItemboxBlock* object=nullptr;uint32_t serial=0;};
    std::array<BlockOwner,32> blocks_{};
    unsigned blockCount_=0;int blockBreak_=-1;
    struct RockOwner {const Kinoko::Field::ObjectRock* object=nullptr;uint32_t serial=0;};
    std::array<RockOwner,32> rocks_{};
    unsigned rockCount_=0;int rockBreak_=-1;
    unsigned moleCount_=0;int moleSpecs_[2]{-1,-1};
    std::array<const Kinoko::Field::ObjectFactoryEffect*,MaxFactoryEffects> factory_{};
    unsigned factoryCount_=0;
    int factorySpecs_[2]{-1,-1};
    struct HanachanOwner {const Kinoko::Field::ObjectHanachan* object=nullptr;std::array<unsigned,7> serial{};};
    std::array<HanachanOwner,4> hanachan_{};
    unsigned hanachanCount_=0;
    int hanachanDust_=-1;
    std::array<const Kinoko::Field::ObjectSanbo*,MaxPokeys> pokeys_{};
    unsigned pokeyCount_=0;
    int pokeySand_=-1;
    struct StatueOwner {const Kinoko::Field::ObjectKoopaFigure* object=nullptr;unsigned serial=0;};
    std::array<StatueOwner,4> statues_{};
    unsigned statueCount_=0;
    int koopaBreath_=-1;
    const Kinoko::Field::ObjectCannon* cannon_=nullptr;
    unsigned cannonSerial_=0;
    int cannonSmoke_=-1,cannonSnow_=-1,starRing_=-1;
    struct CrabOwner {const Kinoko::Field::ObjectCrab* object=nullptr;unsigned serial[3]{};unsigned emergeTicks=0;};
    std::array<CrabOwner,MaxCrabs> crabs_{};
    unsigned crabCount_=0;
    int crabSpecs_[3]{-1,-1,-1};
    struct Owner {const Kinoko::Field::ObjectPenguin* object=nullptr;bool active[2]{};};
    std::array<Owner,MaxPenguins> owners_{};
    unsigned count_=0;bool linked_=false;
    int spinSmall_=-1,spinMedium_=-1,slider_=-1;
};
}
