#include <mkw/luma_visual.hpp>
#include <mkw/vehicle_animation.hpp>
namespace mkw {
bool LumaVisual::load(){
    auto model=std::make_unique<Model>();
    if(!model->load("models/luma.gmdl"))return false;
    auto animation=std::make_unique<DriverAnimation>();
    if(!animation->load("models/luma.dskm",*model))return false;
    const int wait=animation->find("wait");
    if(wait<0||!animation->loop(wait))return false;
    animation->prepareLitSkinning(*model);
    model_=std::move(model);animation_=std::move(animation);wait_=wait;visible_=false;
    return true;
}
void LumaVisual::prepare(const ViewFrustum& view,const VehicleAnimation& vehicle,unsigned tick){
    visible_=false;if(!model_||!animation_)return;
    const auto& motion=vehicle.lumaMotion();const auto scale=vehicle.visualScale();
    const auto x=motion.right()*scale.x,y=motion.up()*scale.y,z=motion.forward()*scale.z,p=motion.position();
    Mtx_Identity(&matrix_);
    matrix_.r[0].x=x.x;matrix_.r[0].y=y.x;matrix_.r[0].z=z.x;matrix_.r[0].w=p.x;
    matrix_.r[1].x=x.y;matrix_.r[1].y=y.y;matrix_.r[1].z=z.y;matrix_.r[1].w=p.y;
    matrix_.r[2].x=x.z;matrix_.r[2].y=y.z;matrix_.r[2].z=z.z;matrix_.r[2].w=p.z;
    auto frustum=view.model(matrix_);
    visible_=animation_->apply(*model_,wait_,vehicle.lumaFrame(),tick,-1,0,&frustum);
}
void LumaVisual::draw(Renderer& renderer)const{
    if(!visible_)return;
    if(animation_->gpuSkinned())renderer.setSkinPalette(animation_->palette(),animation_->matrixCount(),21);
    renderer.draw(*model_,matrix_);
    renderer.setSkinPalette(nullptr,0,12);
}
}
