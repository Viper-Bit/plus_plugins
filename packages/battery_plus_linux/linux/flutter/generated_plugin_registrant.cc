//
//  Generated file. Do not edit.
//

#include "generated_plugin_registrant.h"

#include <battery_plus_linux/battery_plus_linux_plugin.h>

void fl_register_plugins(FlPluginRegistry* registry) {
  g_autoptr(FlPluginRegistrar) battery_plus_linux_registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry, "BatteryPlusLinuxPlugin");
  battery_plus_linux_plugin_register_with_registrar(battery_plus_linux_registrar);
}
