#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string>
#include <cstring>
#include <iostream>

// IMPORTANTE: Ajuste aqui o tamanho exato da sua imagem wallpaper.png
#define GUI_WIDTH  704
#define GUI_HEIGHT 384
#define UI_URI "http://realsigmamusic.com/plugins/mydrum7#ui"

typedef struct {
    LV2UI_Write_Function write = nullptr;
    LV2UI_Controller     controller = nullptr;

    Display* display = nullptr;
    Window   window = 0;
    cairo_surface_t* background = nullptr;
    Atom wm_delete_window;

    bool quit = false;
} MyUI;

#include "wallpaper.h"

struct PngReaderState {
    const unsigned char* data;
    unsigned int length;
    unsigned int offset;
};

static cairo_status_t read_png_from_memory(void *closure, unsigned char *data, unsigned int length) {
    PngReaderState *state = (PngReaderState *)closure;
    if (state->offset + length > state->length) {
        return CAIRO_STATUS_READ_ERROR;
    }
    memcpy(data, state->data + state->offset, length);
    state->offset += length;
    return CAIRO_STATUS_SUCCESS;
}

static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor,
                                const char* plugin_uri,
                                const char* bundle_path,
                                LV2UI_Write_Function write_function,
                                LV2UI_Controller controller,
                                LV2UI_Widget* widget,
                                const LV2_Feature* const* features) {

    MyUI* ui = new (std::nothrow) MyUI;
    if (!ui) {
        return NULL;
    }
    ui->write = write_function;
    ui->controller = controller;

    // 1. Encontrar a janela Pai (Host)
    void* parentXwindow = NULL;
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_UI__parent)) {
            parentXwindow = features[i]->data;
            break;
        }
    }

    if (!parentXwindow) {
        std::cerr << "[MyDrum7 UI] Error: Host did not provide a parent window." << std::endl;
        delete ui;
        return NULL;
    }

    // Initialize X11 threading support
    if (!XInitThreads()) {
        // Just a warning, not fatal usually
    }

    ui->display = XOpenDisplay(NULL);
    if (!ui->display) {
        std::cerr << "[MyDrum7 UI] Error: Cannot open X11 display." << std::endl;
        delete ui;
        return NULL;
    }

    Window parent = (Window)parentXwindow;

    // 2. Carregar imagem da memória (Embutido no binário)
    PngReaderState state = { wallpaper_png, wallpaper_png_len, 0 };
    ui->background = cairo_image_surface_create_from_png_stream(
        read_png_from_memory, 
        &state
    );

    if (cairo_surface_status(ui->background) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "[MyDrum7 UI] Error: Failed to load built-in wallpaper." << std::endl;
    }

    // 3. Criar a janela usando XCreateWindow para garantir match de Visual/Depth
    XSetWindowAttributes attrs;
    unsigned long mask = CWBackPixel | CWBorderPixel | CWEventMask;

    // Inherit from parent (which usually implies same Visual/Depth)
    attrs.background_pixel = 0; // Black
    attrs.border_pixel = 0;
    attrs.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask;

    ui->window = XCreateWindow(
        ui->display, parent,
        0, 0, GUI_WIDTH, GUI_HEIGHT,
        0,                          // border width
        CopyFromParent,             // depth (must match parent)
        InputOutput,                // class
        CopyFromParent,             // visual (must match parent)
        mask,
        &attrs
    );

    if (!ui->window) {
        std::cerr << "[MyDrum7 UI] Error: XCreateWindow failed." << std::endl;
        XCloseDisplay(ui->display);
        delete ui;
        return NULL;
    }

    // 4. Configurar protocolos
    ui->wm_delete_window = XInternAtom(ui->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->display, ui->window, &ui->wm_delete_window, 1);

    XMapWindow(ui->display, ui->window);
    XFlush(ui->display);

    // FORCE an initial Expose event
    XClearArea(ui->display, ui->window, 0, 0, 0, 0, True);
    XFlush(ui->display);

    *widget = (LV2UI_Widget)ui->window;
    return (LV2UI_Handle)ui;
}

static void cleanup(LV2UI_Handle handle) {
    MyUI* ui = (MyUI*)handle;
    if (ui->background) cairo_surface_destroy(ui->background);
    if (ui->window) XDestroyWindow(ui->display, ui->window);
    if (ui->display) XCloseDisplay(ui->display);
    delete ui;
}

static int ui_idle(LV2UI_Handle handle) {
    MyUI* ui = (MyUI*)handle;

    if (ui->quit) {
        return 1; // Close UI
    }

    bool drew = false;

    // Processar eventos do X11
    while (XPending(ui->display) > 0) {
        XEvent xev;
        XNextEvent(ui->display, &xev);

        if (xev.type == ClientMessage && (Atom)xev.xclient.data.l[0] == ui->wm_delete_window) {
            ui->quit = true;
        } else if (xev.type == Expose && xev.xexpose.count == 0) {

            XWindowAttributes wa;
            XGetWindowAttributes(ui->display, ui->window, &wa);

            cairo_surface_t* xsurface = cairo_xlib_surface_create(
                ui->display, ui->window, 
                wa.visual, 
                wa.width, wa.height
            );
            cairo_t* cr = cairo_create(xsurface);

            // Draw Background
            if (ui->background && cairo_surface_status(ui->background) == CAIRO_STATUS_SUCCESS) {
                cairo_set_source_surface(cr, ui->background, 0, 0);
                cairo_paint(cr);
            } else {
                // Fallback: Red box
                cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Dark Grey
                cairo_paint(cr);
                cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); // Red box
                cairo_rectangle(cr, 10, 10, 50, 50);
                cairo_fill(cr);
            }

            cairo_destroy(cr);
            cairo_surface_destroy(xsurface);
            drew = true;
        }
    }

    if (drew) {
        XFlush(ui->display);
    }

    return 0;
}

static const LV2UI_Idle_Interface idle_interface = { ui_idle };

static const void* extension_data(const char* uri) {
    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle_interface;
    }
    return NULL;
}

static const LV2UI_Descriptor descriptor = {
    UI_URI,
    instantiate,
    cleanup,
    NULL,
    extension_data
};

extern "C" {
    LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) {
        return (index == 0) ? &descriptor : NULL;
    }
}
