#include "../engine/Modules/ContainedLiquidMath.h"
#include <cstdio>
#include <cstdlib>
void Require(bool value) { if(!value) std::abort(); }
int main()
{
    using namespace PIXL::ContainedLiquidMath;
    for (int i=2;i<=98;++i) {
        float f=i/100.0f,h=FillHeight(f);
        Require(std::abs((2+3*h-h*h*h)*0.25f-f)<0.00001f);
    }
    const std::array<float,16> bottleProfile{
        0.42f,0.61f,0.78f,0.90f,0.95f,0.97f,0.96f,0.94f,
        0.92f,0.89f,0.85f,0.78f,0.69f,0.57f,0.43f,0.30f};
    for (const auto plane : {std::array<float,3>{0.0f,0.0f,3.4f},
             std::array<float,3>{2.7f,0.4f,2.1f},
             std::array<float,3>{-1.9f,2.2f,0.35f}}) {
        const float support=std::hypot(plane[0],plane[1])*
            *std::max_element(bottleProfile.begin(),bottleProfile.end())+std::abs(plane[2]);
        const float total=ProfileVolumeBelow(bottleProfile,plane,support+1.0f);
        for (int i=2;i<=98;i+=4) {
            const float fraction=i/100.0f;
            const float offset=ProfileFillOffset(bottleProfile,plane,fraction);
            const float measured=ProfileVolumeBelow(bottleProfile,plane,offset)/total;
            Require(std::abs(measured-fraction)<0.0002f);
        }
    }
    float baseline=0;
    for (int fps : {24,30,60,120,240}) {
        float x=0,v=0;
        for(int i=0;i<fps;++i) Spring(x,v,0.12f,0.55f,1.0f/fps);
        if(fps==24)baseline=x;else Require(std::abs(x-baseline)<0.00001f);
        for(int i=0;i<fps*10;++i)Spring(x,v,0,0.55f,1.0f/fps);
        Require(std::abs(x)+std::abs(v)<0.00001f);
    }
    float x=0,v=0;
    for(int i=0;i<100000;++i) {
        Spring(x,v,(i%2?1:-1)*0.36f,0.25f,0.001f+(i%200)*0.001f);
        Require(std::isfinite(x)&&std::isfinite(v)&&std::abs(x)<=0.22f);
    }
    std::puts("PASS: ellipsoid/profile fill fractions, tilted volume preservation, 24..240 FPS invariance, settling and 100000 bounded variable-step impulses");
}
