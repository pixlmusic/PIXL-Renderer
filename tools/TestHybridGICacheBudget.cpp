#include "../engine/Modules/HybridGI/CacheBudget.h"
#include <limits>
#include <iostream>
int main()
{
    PIXL::HybridGIInternal::CacheBudget budget;
    auto require=[](bool value) { if(!value) throw "Cache budget invariant"; };
    try {
        using PIXL::HybridGIInternal::CacheMilliseconds;
        require(CacheMilliseconds(1,0.5f)==187);
        require(CacheMilliseconds(8,0.0f)==1000);
        require(CacheMilliseconds(0,0.0f)-CacheMilliseconds(0xffffffffu,0.992f)==1u);
        require(CacheMilliseconds(0,std::numeric_limits<float>::quiet_NaN())==0);
        require(budget.Update(0.0f,false)==1.0f);
        float last=1.0f;
        for(int i=0;i<32;++i) { float f=budget.Update(1.0f,true); require(f>=0.25f && f<=last && last-f<=0.0625f); last=f; }
        require(last==0.25f);
        require(budget.Update(0.15f,true)==0.25f);
        require(budget.Update(std::numeric_limits<float>::quiet_NaN(),true)==1.0f);
        for(int i=0;i<16;++i) budget.Update(1.0f,true);
        for(int i=0;i<16;++i) last=budget.Update(0.01f,true);
        require(last==1.0f);
        require(budget.Update(-1.0f,true)==1.0f);
        std::cout<<"PASS: bounded cache budget, hysteresis, recovery and invalid timestamp fallback\n";
    } catch(...) { return 1; }
}
