#pragma once
#include <mkw/coin_battle.hpp>
#include <algorithm>
#include <limits>

namespace mkw {
// CoinUnit80868d54..69014. The initial sphere may already intersect the
// course: first escape upwards, then descend/refine the first contact.
// Shares the original1000-iteration budget across both phases.
struct CoinGround {
    Vec3 position{};
    ItemSurface surface{};
    unsigned iterations=0;
    static CoinGround probe(Vec3 position,ItemWorldQuery query,float radius=150.f){
        CoinGround out;out.position=position;
        if(!query)return out;
        const float distance=std::min(299.f,1.9f*radius);
        const float inf=std::numeric_limits<float>::infinity();
        while(out.iterations<1000&&position.y>=0){
            out.surface=query({inf,inf,inf},position,radius);
            if(!out.surface.collided){
                Vec3 step{0,-distance,0};
                while(out.iterations<1000&&position.y>=0){
                    const auto next=position+step;
                    out.surface=query(position,next,radius);
                    if(out.surface.collided){
                        if(length(step)<10.f){out.position=next+out.surface.correction;return out;}
                        step=step*.5f;
                    }else position=next;
                    ++out.iterations;
                }
                break;
            }
            position.y+=distance;
            ++out.iterations;
        }
        out.position=position;
        return out;
    }
    static bool place(CoinBody& body,ItemWorldQuery query){
        const auto ground=probe(body.position,query,CoinBody::Radius);
        if(ground.position.y<0)return false;
        // State1 accepts outputY only; walls must not shift authoredX/Z.
        body.position.y=ground.position.y;body.previous=body.position;
        body.floorNormal=ground.surface.floorNormal;body.floorObject=ground.surface.floorObject;
        body.boundCentreY=body.position.y;body.boundObjectY=ground.surface.floorObjectY;
        return true;
    }
    static bool follow(CoinBody& body,unsigned course,float objectY){
        if(course!=36||!body.floorObject)return false;
        body.position.y=followHeight(body.boundCentreY,body.boundObjectY,objectY);
        body.previous=body.position;return true;
    }
    // Course36's Sandcone has fixed rotation/scale and translates onlyY.
    // The original callback returns its bound floorY, then CoinUnit adds150;
    // retaining the bound centre and applying that same translation delta
    // gives the same height without moving the original X/Z.
    static float followHeight(float boundCentre,float boundObject,float object){
        return boundCentre+(object-boundObject);
    }
};
} // namespace mkw
