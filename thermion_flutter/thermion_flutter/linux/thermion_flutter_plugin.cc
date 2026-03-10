#include "include/thermion_flutter/thermion_flutter_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <flutter_linux/fl_texture_registrar.h>
#include <flutter_linux/fl_texture_gl.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <cstring>
#include <iostream>
#include <map>

#include "include/thermion_flutter/filament_texture.h"

#include <epoxy/gl.h>
#include <epoxy/glx.h>

#define FLUTTER_FILAMENT_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), thermion_flutter_plugin_get_type(), \
                              ThermionFlutterPlugin))

struct _ThermionFlutterPlugin {
  GObject parent_instance;
  FlTextureRegistrar* texture_registrar;
  FlView* fl_view;
  // Map from Flutter texture ID to the FlTexture object.
  std::map<int64_t, FlTexture*>* textures;
};

G_DEFINE_TYPE(ThermionFlutterPlugin, thermion_flutter_plugin, g_object_get_type())

// ---------------------------------------------------------------------------
// Method-channel handlers
//
// The Dart side (ThermionFlutterPluginImpl) communicates with this native
// plugin over the "dev.thermion.flutter/event" method channel.  In the
// current architecture, all Filament/rendering work is handled by
// thermion_dart via Dart FFI.  This plugin only manages Flutter texture
// registration and returns platform-specific handles.
// ---------------------------------------------------------------------------

static FlMethodResponse* handle_get_driver_platform(
    ThermionFlutterPlugin* self, FlMethodCall* method_call) {
  // Return 0 (nullptr) — on Linux the Dart side passes nullptr for the
  // platform handle, which tells Filament to auto-detect.
  g_autoptr(FlValue) result = fl_value_new_int(0);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* handle_get_shared_context(
    ThermionFlutterPlugin* self, FlMethodCall* method_call) {
  auto context = glXGetCurrentContext();
  g_autoptr(FlValue) result =
      fl_value_new_int(reinterpret_cast<int64_t>(context));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* handle_create_texture(
    ThermionFlutterPlugin* self, FlMethodCall* method_call) {
  FlValue* args = fl_method_call_get_args(method_call);

  const int64_t width = fl_value_get_int(fl_value_get_list_value(args, 0));
  const int64_t height = fl_value_get_int(fl_value_get_list_value(args, 1));

  auto texture = create_filament_texture(
      static_cast<uint32_t>(width),
      static_cast<uint32_t>(height),
      self->texture_registrar);

  if (texture == nullptr) {
    g_autoptr(FlValue) result = fl_value_new_list();
    fl_value_append_take(result, fl_value_new_int(-1));
    fl_value_append_take(result, fl_value_new_int(0));
    fl_value_append_take(result, fl_value_new_int(0));
    return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
  }

  auto flutterTextureId = fl_texture_get_id(texture);
  auto hardwareTextureId =
      static_cast<int64_t>(FILAMENT_TEXTURE_GL(texture)->texture_id);

  (*self->textures)[flutterTextureId] = texture;

  g_autoptr(FlValue) result = fl_value_new_list();
  fl_value_append_take(result, fl_value_new_int(flutterTextureId));
  fl_value_append_take(result, fl_value_new_int(hardwareTextureId));
  fl_value_append_take(result, fl_value_new_int(0));  // window handle (unused)
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* handle_destroy_texture(
    ThermionFlutterPlugin* self, FlMethodCall* method_call) {
  FlValue* args = fl_method_call_get_args(method_call);
  auto flutterTextureId = fl_value_get_int(args);

  auto it = self->textures->find(flutterTextureId);
  if (it != self->textures->end()) {
    destroy_filament_texture(it->second, self->texture_registrar);
    self->textures->erase(it);
  }

  g_autoptr(FlValue) result = fl_value_new_bool(true);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* handle_mark_texture_frame_available(
    ThermionFlutterPlugin* self, FlMethodCall* method_call) {
  FlValue* args = fl_method_call_get_args(method_call);
  auto flutterTextureId = fl_value_get_int(args);

  auto it = self->textures->find(flutterTextureId);
  if (it != self->textures->end()) {
    fl_texture_registrar_mark_texture_frame_available(
        self->texture_registrar, it->second);
  }

  g_autoptr(FlValue) result = fl_value_new_bool(true);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

// ---------------------------------------------------------------------------
// Plugin lifecycle
// ---------------------------------------------------------------------------

static void thermion_flutter_plugin_handle_method_call(
    ThermionFlutterPlugin* self,
    FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method = fl_method_call_get_name(method_call);

  if (strcmp(method, "getDriverPlatform") == 0) {
    response = handle_get_driver_platform(self, method_call);
  } else if (strcmp(method, "getSharedContext") == 0) {
    response = handle_get_shared_context(self, method_call);
  } else if (strcmp(method, "createTexture") == 0) {
    response = handle_create_texture(self, method_call);
  } else if (strcmp(method, "destroyTexture") == 0) {
    response = handle_destroy_texture(self, method_call);
  } else if (strcmp(method, "markTextureFrameAvailable") == 0) {
    response = handle_mark_texture_frame_available(self, method_call);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

static void thermion_flutter_plugin_dispose(GObject* object) {
  auto plugin = reinterpret_cast<ThermionFlutterPlugin*>(object);
  delete plugin->textures;
  plugin->textures = nullptr;
  G_OBJECT_CLASS(thermion_flutter_plugin_parent_class)->dispose(object);
}

static void thermion_flutter_plugin_class_init(
    ThermionFlutterPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = thermion_flutter_plugin_dispose;
}

static void thermion_flutter_plugin_init(ThermionFlutterPlugin* self) {
  self->textures = new std::map<int64_t, FlTexture*>();
}

static void method_call_cb(FlMethodChannel* channel,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  ThermionFlutterPlugin* plugin = FLUTTER_FILAMENT_PLUGIN(user_data);
  thermion_flutter_plugin_handle_method_call(plugin, method_call);
}

void thermion_flutter_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  ThermionFlutterPlugin* plugin = FLUTTER_FILAMENT_PLUGIN(
      g_object_new(thermion_flutter_plugin_get_type(), nullptr));

  FlView* fl_view = fl_plugin_registrar_get_view(registrar);
  plugin->fl_view = fl_view;

  plugin->texture_registrar =
      fl_plugin_registrar_get_texture_registrar(registrar);

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  g_autoptr(FlMethodChannel) channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "dev.thermion.flutter/event",
                            FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(channel, method_call_cb,
                                            g_object_ref(plugin),
                                            g_object_unref);

  g_object_unref(plugin);
}
