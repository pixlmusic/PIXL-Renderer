// Checks the linear SH rotation and scalar horizon projection used by GI.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
using V = std::array<float, 3>;
using SH = std::array<float, 4>;
float dot(V a, V b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
V scale(V a, float b) { for(auto& v:a) v*=b; return a; }
V rotate(V a, float angle) {
    const float s=std::sin(angle), c=std::cos(angle);
    return {c*a[0]-s*a[2], a[1], s*a[0]+c*a[2]};
}
SH evaluate(V d) { return {0.28209479177387814f,-0.48860251190291992f*d[1],0.48860251190291992f*d[2],-0.48860251190291992f*d[0]}; }
int main() {
    std::mt19937 random(42);
    std::uniform_real_distribution<float> unit(-1,1), energy(0,8);
    float maximumRelativeError=0;
    for(int test=0;test<10000;++test) {
        SH before{}, after{};
        const float angle=unit(random)*3.14159265f;
        for(int sample=0;sample<144;++sample) {
            V d={unit(random),unit(random),unit(random)};
            d=scale(d,1/std::sqrt(std::max(dot(d,d),1e-8f)));
            const float weight=energy(random);
            auto a=evaluate(rotate(d,angle)), b=evaluate(d);
            for(int i=0;i<4;++i) { before[i]+=a[i]*weight; after[i]+=b[i]*weight; }
            V back={unit(random)*500,unit(random)*500,unit(random)*500};
            const float inv=1/std::sqrt(std::max(dot(back,back),1e-8f));
            assert(std::abs(dot(scale(back,inv),d)-dot(back,d)*inv)<1e-6f);
        }
        V moment=rotate({-after[3],-after[1],after[2]},angle);
        after[1]=-moment[1]; after[2]=moment[2]; after[3]=-moment[0];
        for(int i=0;i<4;++i) {
            float relative=std::abs(after[i]-before[i])/std::max(before[0],1e-6f);
            maximumRelativeError=std::max(maximumRelativeError,relative);
            assert(relative<1e-5f);
        }
    }
    std::cout<<"GI algebra: 10000 cases passed; maximum error relative to L0 = "<<maximumRelativeError<<"\n";
}
