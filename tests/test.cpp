#include "../ESP32_Weather/WeatherLogic.h"
#include <cassert>
int main(){
 assert(validWeather(-12.5,0,0));assert(validWeather(45,100,200));
 assert(!validWeather(NAN,50,10));assert(!validWeather(20,101,5));
 assert(!validWeather(20,40,-1));assert(!validWeather(100,40,5));
 assert(!expired(100,0));assert(expired(1200001,0));
 assert(expired(uint32_t(0xffffff00u+1200001u),0xffffff00u));
}
