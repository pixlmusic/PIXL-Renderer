#include "HybridGIGeometryPrototype.h"
#include <iostream>
#include <limits>
#include <vector>
int main()
{
    using namespace PIXL::HybridGIGeometryPrototype;
    auto require=[](bool value) { if(!value) throw "Geometry adapter invariant"; };
    try {
        Triangle wall{{-9,-3,-6},{-3,-3,-6},{-9,3,-6}};
        std::vector<Triangle> triangles{wall};
        auto result=Convert(triangles,10,2,10,2);
        require(result.count==1 && !result.overflow);
        require(result.surfels[0].position==Point{-7,-1,-6});
        require(result.surfels[0].normal==Point{0,0,1});
        require(result.surfels[0].state==6 && result.surfels[0].confidence==0.25f);
        require(Convert(triangles,9,2,10,2).count==0);
        require(Convert(triangles,10,1,10,2).count==0);
        Triangle invalid=wall; invalid.deforming=true; triangles.push_back(invalid);
        invalid=wall; invalid.transparent=true; triangles.push_back(invalid);
        invalid=wall; invalid.b=invalid.a; triangles.push_back(invalid);
        invalid=wall; invalid.a[0]=std::numeric_limits<float>::quiet_NaN(); triangles.push_back(invalid);
        require(Convert(triangles,10,2,10,2).count==1);
        triangles.assign(1000,wall); result=Convert(triangles,10,2,10,2);
        require(result.count==64 && result.overflow);
        std::cout<<"PASS: owned triangle adapter, bounded work/output, frame/epoch rejection and occluder-only evidence; engine source remains unverified/disabled\n";
    } catch(...) { return 1; }
}
