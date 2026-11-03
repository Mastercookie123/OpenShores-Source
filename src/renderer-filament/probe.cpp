#include <filament/Engine.h>
extern "C" __declspec(dllexport) int openshores_filament_probe() {
 auto* engine=filament::Engine::create(filament::Engine::Backend::VULKAN);
 if(!engine)return 0;
 filament::Engine::destroy(&engine);return 1;
}