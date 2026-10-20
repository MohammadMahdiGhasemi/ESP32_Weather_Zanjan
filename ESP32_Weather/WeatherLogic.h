#pragma once
#include <math.h>
#include <stdint.h>
inline bool validWeather(float t,float h,float w){
 return isfinite(t)&&isfinite(h)&&isfinite(w)&&t>=-90&&t<=65&&h>=0&&h<=100&&w>=0&&w<=500;
}
inline bool expired(uint32_t now,uint32_t last){return uint32_t(now-last)>1200000u;}
