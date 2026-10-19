#pragma once
#include <cmath>
namespace mkw {
// Original NW4R Y (5) and PERSP_ROT (4), used by course Mii head planes.
// Work in view space, then return the basis to model space; translation stays
// at its authored animated joint. Y preserves the local up axis, PERSP_ROT
// faces the eye point instead of the parallel camera plane.
inline bool billboardMiiJoint(float* joint,const float* modelView,unsigned mode){
    if(mode!=4&&mode!=5)return false;
    float c[12]{};
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<4;++k){
        for(unsigned j=0;j<3;++j)c[r*4+k]+=modelView[r*4+j]*joint[j*4+k];
        if(k==3)c[r*4+k]+=modelView[r*4+3];
    }
    const float a=modelView[0],b=modelView[1],d=modelView[2],e=modelView[4],f=modelView[5],g=modelView[6],h=modelView[8],i=modelView[9],j=modelView[10];
    const float determinant=a*(f*j-g*i)-b*(e*j-g*h)+d*(e*i-f*h);
    if(std::fabs(determinant)<1e-12f)return false;
    const float inverse[9]={(f*j-g*i)/determinant,(d*i-b*j)/determinant,(b*g-d*f)/determinant,
        (g*h-e*j)/determinant,(a*j-d*h)/determinant,(d*e-a*g)/determinant,
        (e*i-f*h)/determinant,(b*h-a*i)/determinant,(a*f-b*e)/determinant};
    auto normalize=[](float* v){float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(n<1e-8f)return false;for(unsigned q=0;q<3;++q)v[q]/=n;return true;};
    auto cross=[](const float* u,const float* v,float* w){const float x=u[1]*v[2]-u[2]*v[1],y=u[2]*v[0]-u[0]*v[2],z=u[0]*v[1]-u[1]*v[0];w[0]=x;w[1]=y;w[2]=z;};
    float scales[3];for(unsigned q=0;q<3;++q)scales[q]=std::sqrt(c[q]*c[q]+c[4+q]*c[4+q]+c[8+q]*c[8+q]);
    if(scales[0]<1e-8f||scales[1]<1e-8f||scales[2]<1e-8f)return false;
    float up[3]={c[1]/scales[1],c[5]/scales[1],c[9]/scales[1]};
    float z[3]={0,0,1};if(mode==4){z[0]=-c[3];z[1]=-c[7];z[2]=-c[11];if(!normalize(z))return false;}
    float x[3],y[3];cross(up,z,x);if(!normalize(x))return false;
    if(mode==5){for(unsigned q=0;q<3;++q)y[q]=up[q];cross(x,y,z);if(!normalize(z))return false;}
    else {cross(z,x,y);if(!normalize(y))return false;}
    const float* axes[]={x,y,z};
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k){joint[r*4+k]=0;for(unsigned q=0;q<3;++q)joint[r*4+k]+=inverse[r*3+q]*axes[k][q]*scales[k];}
    return true;
}
// NW4R BILLBOARD (mode 1): view-plane normal, projected local Y roll,
// original column lengths and origin. Matrices are row-major affine 3x4.
// ROT nodes may supply the original parent-cancelled XY roll axis instead.
inline bool billboardJoint(float* joint,const float* modelView,const float* rollAxis=nullptr){
    float a[9],c[9];
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k){
        a[r*3+k]=modelView[r*4+k];c[r*3+k]=0;
        for(unsigned j=0;j<3;++j)c[r*3+k]+=modelView[r*4+j]*joint[j*4+k];
    }
    const float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if(std::fabs(det)<1e-12f)return false;
    const float inv[9]={(a[4]*a[8]-a[5]*a[7])/det,(a[2]*a[7]-a[1]*a[8])/det,(a[1]*a[5]-a[2]*a[4])/det,
        (a[5]*a[6]-a[3]*a[8])/det,(a[0]*a[8]-a[2]*a[6])/det,(a[2]*a[3]-a[0]*a[5])/det,
        (a[3]*a[7]-a[4]*a[6])/det,(a[1]*a[6]-a[0]*a[7])/det,(a[0]*a[4]-a[1]*a[3])/det};
    float scale[3];for(unsigned k=0;k<3;++k)scale[k]=std::sqrt(c[k]*c[k]+c[3+k]*c[3+k]+c[6+k]*c[6+k]);
    const float rx=rollAxis?rollAxis[0]:c[1],ry=rollAxis?rollAxis[1]:c[4];
    const float len=std::hypot(rx,ry);
    const float x=len>1e-8f?rx/len:0.f,y=len>1e-8f?ry/len:1.f;
    const float b[9]={scale[0]*y,scale[1]*x,0,-scale[0]*x,scale[1]*y,0,0,0,scale[2]};
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k){
        joint[r*4+k]=0;for(unsigned j=0;j<3;++j)joint[r*4+k]+=inv[r*3+j]*b[j*3+k];
    }
    return true;
}
}
