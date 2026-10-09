// Checks the linear SH rotation and scalar horizon projection used by GI.
#ifdef NDEBUG
#undef NDEBUG
#endif
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
// Scalar model of SHHallucinateZH3Irradiance; shader compilation is checked separately.
float irradiance(SH sh, V direction, bool guarded) {
    const float pi = 3.14159265358979323846f;
    const SH cosine = {0.8862269254527580137f,
        -1.0233267079464884885f*direction[1],
        1.0233267079464884885f*direction[2],
        -1.0233267079464884885f*direction[0]};
    float result = 0;
    for (int i = 0; i < 4; ++i) result += sh[i]*cosine[i];
    const V moment = {sh[3], sh[1], sh[2]};
    const float lengthSq = dot(moment, moment);
    const V axis = scale(moment, 1/std::sqrt(guarded ? std::max(lengthSq,1e-12f) : lengthSq));
    const float ratio = std::abs(dot({-sh[3],-sh[1],sh[2]}, axis))/(guarded ? std::max(sh[0],1e-8f) : sh[0]);
    const float l2 = (guarded ? std::max(sh[0],0.0f) : sh[0])*(0.08f*ratio + 0.6f*ratio*ratio);
    const float z = dot(axis, direction);
    result += 0.25f*l2*std::sqrt(5/(16*pi))*(3*z*z-1);
    return std::max(0.0f, result);
}
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
    assert(irradiance({}, {0,0,1}, true) == 0);
    for (const V direction : {V{0,0,1}, V{0,1,0}, V{1,0,0}, V{0,0,0}}) {
        const float uniform = irradiance({2,0,0,0}, direction, true);
        assert(std::isfinite(uniform));
        assert(std::abs(uniform-2*0.8862269254527580137f)<1e-6f);
        assert(irradiance({-1,0,0,0}, direction, true) == 0);
    }
    for (int test=0; test<10000; ++test) {
        const SH sh = {1+energy(random), unit(random), unit(random), unit(random)};
        const V direction = {unit(random), unit(random), unit(random)};
        const float before = irradiance(sh, direction, false);
        const float after = irradiance(sh, direction, true);
        assert(std::isfinite(after));
        assert(before == after);
    }
    std::cout<<"SH irradiance: black/isotropic/nonpositive cases and 10000 directional equivalence cases passed\n";
}
