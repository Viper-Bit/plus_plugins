#include "include/battery_plus_linux/battery_plus_linux_plugin.h"
#include "upower_device.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <cmath>

#define BATTERY_PLUS_LINUX_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), battery_plus_linux_plugin_get_type(), \
                              BatteryPlusLinuxPlugin))

static const char* kMethodChannel = "plugins.flutter.io/battery";
static const char* kEventChannel = "plugins.flutter.io/charging";
static const char* kBatteryLevelMethod = "getBatteryLevel";
static const char* kDbusError = "D-BUS Error";
static const char* kDbusInterface = "org.freedesktop.UPower";
static const char* kDbusObject = "/org/freedesktop/UPower/devices/DisplayDevice";

struct _BatteryPlusLinuxPlugin {
  GObject parent_instance;
  UPowerDevice* state_device = nullptr;
};

G_DEFINE_TYPE(BatteryPlusLinuxPlugin, battery_plus_linux_plugin, g_object_get_type())

enum UPowerState {
  Unknown = 0,
  Charging = 1,
  Discharging = 2,
  Empty = 3,
  FullyCharged = 4,
  PendingCharge = 5,
  PendingDischarge = 6
};

// ### TODO: introduce a new 'unknown' battery state for workstations?
static const gchar *upower_state_str(guint state) {
  switch (state) {
  case Charging:
    return "charging";
  case Discharging:
    return "discharging";
  default:
    return "full";
  }
}

static UPowerDevice* upower_device_new(FlMethodResponse **response) {
  g_autoptr(GError) error = nullptr;
  UPowerDevice* device = upower_device_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                              G_DBUS_PROXY_FLAGS_NONE,
                                                              kDbusInterface,
                                                              kDbusObject,
                                                              nullptr,
                                                              &error);
  if (error) {
    *response = FL_METHOD_RESPONSE(fl_method_error_response_new(kDbusError,
                                                                error->message,
                                                                nullptr));
  }
  return device;
}

static void battery_plus_linux_plugin_handle_method_call(BatteryPlusLinuxPlugin* self,
                                                         FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method_name = fl_method_call_get_name(method_call);
  if (strcmp(method_name, kBatteryLevelMethod) == 0) {
    UPowerDevice* device = upower_device_new(&response);
    if (!response) {
      gdouble level = upower_device_get_percentage(device);
      g_autoptr(FlValue) result = fl_value_new_int(std::round(level));
      g_object_unref(device);
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
    }
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }
  fl_method_call_respond(method_call, response, nullptr);
}

static void send_battery_state_event(FlEventChannel *event_channel, guint state) {
  g_autoptr(FlValue) message = fl_value_new_string(upower_state_str(state));
  g_autoptr(GError) error = nullptr;
  if (!fl_event_channel_send(event_channel, message, NULL, &error)) {
    g_warning("Failed to send event: %s", error->message);
  }
}

static void properties_changed_cb(GDBusProxy* proxy,
                                  GVariant* changed_properties,
                                  const gchar* invalidated_properties,
                                  gpointer user_data)
{
  GVariant* value = g_variant_lookup_value(changed_properties,
                                           "State",
                                           G_VARIANT_TYPE_UINT32);
  if (value) {
    guint32 state = g_variant_get_uint32(value);
    send_battery_state_event(FL_EVENT_CHANNEL(user_data), state);
  }
}

static FlMethodErrorResponse* battery_plus_linux_plugin_listen_events(BatteryPlusLinuxPlugin* self,
                                                                      FlEventChannel* event_channel,
                                                                      FlValue* args) {
  FlMethodResponse *response = nullptr;
  self->state_device = upower_device_new(&response);
  if (self->state_device) {
    g_signal_connect(self->state_device,
                     "g-properties-changed",
                     G_CALLBACK(properties_changed_cb),
                     event_channel);
  }
  send_battery_state_event(event_channel, upower_device_get_state(self->state_device));
  return FL_METHOD_ERROR_RESPONSE(response);
}

static FlMethodErrorResponse* battery_plus_linux_plugin_cancel_events(BatteryPlusLinuxPlugin* self,
                                                                      FlEventChannel* event_channel,
                                                                      FlValue* args) {
  g_clear_object(&self->state_device);
  return nullptr;
}

static void battery_plus_linux_plugin_dispose(GObject* object) {
  G_OBJECT_CLASS(battery_plus_linux_plugin_parent_class)->dispose(object);
}

static void battery_plus_linux_plugin_class_init(BatteryPlusLinuxPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = battery_plus_linux_plugin_dispose;
}

static void battery_plus_linux_plugin_init(BatteryPlusLinuxPlugin* self) { }

static void method_call_cb(FlMethodChannel* method_channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  BatteryPlusLinuxPlugin* plugin = BATTERY_PLUS_LINUX_PLUGIN(user_data);
  battery_plus_linux_plugin_handle_method_call(plugin, method_call);
}

static FlMethodErrorResponse* listen_cb(FlEventChannel* event_channel,
                                        FlValue* args,
                                        gpointer user_data) {
  BatteryPlusLinuxPlugin* plugin = BATTERY_PLUS_LINUX_PLUGIN(user_data);
  return battery_plus_linux_plugin_listen_events(plugin, event_channel, args);
}

static FlMethodErrorResponse* cancel_cb(FlEventChannel* event_channel,
                                        FlValue* args,
                                        gpointer user_data) {
  BatteryPlusLinuxPlugin* plugin = BATTERY_PLUS_LINUX_PLUGIN(user_data);
  return battery_plus_linux_plugin_cancel_events(plugin, event_channel, args);
}

void battery_plus_linux_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  BatteryPlusLinuxPlugin* plugin =
      BATTERY_PLUS_LINUX_PLUGIN(g_object_new(battery_plus_linux_plugin_get_type(), nullptr));

  FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

  g_autoptr(FlMethodChannel) method_channel = fl_method_channel_new(messenger,
                                                                    kMethodChannel,
                                                                    FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(method_channel,
                                            method_call_cb,
                                            g_object_ref(plugin),
                                            g_object_unref);

  g_autoptr(FlEventChannel) event_channel = fl_event_channel_new(messenger,
                                                                 kEventChannel,
                                                                 FL_METHOD_CODEC(codec));
  fl_event_channel_set_stream_handlers(event_channel,
                                       listen_cb,
                                       cancel_cb,
                                       g_object_ref(plugin),
                                       g_object_unref);

  g_object_unref(plugin);
}
