# Adding LVGL

The demo in this repo is intentionally LVGL-free so the panel path is unambiguous. Once solid colors render, layering LVGL 8.3/8.4 is straightforward — but there are four traps that crash before you ever see a pixel.

## 1. Call `lv_init()` first — before *any* other LVGL API

Skipping it leaves LVGL's global timer linked-list uninitialized, so the first internal allocation in `lv_disp_drv_register()` requests a garbage size:

```text
assert failed: block_locate_free tlsf.c:566 (block_size(block) >= size)
  lv_mem_alloc → _lv_ll_ins_head → lv_timer_create → lv_disp_drv_register
```

This looks like heap corruption; it isn't. It's a missing `lv_init()`.

```c
lv_init();                       // FIRST
lv_disp_drv_t disp_drv;
lv_disp_drv_init(&disp_drv);
// ... panel + buffers ...
lv_disp_drv_register(&disp_drv);
```

## 2. Color config

`lv_conf.h` (or Kconfig):
```c
#define LV_COLOR_DEPTH    16
#define LV_COLOR_16_SWAP   1   // AXS15231B expects big-endian RGB565
```

## 3. LVGL is single-threaded — one mutex

If you render in a task (`lv_timer_handler`) and mutate widgets from another task (status updates, network callbacks), you get a fault deep in layout:

```text
Guru Meditation (LoadProhibited)  get_prop_core → lv_obj_get_style_prop → layout_update_core
```

Wrap the render loop **and** every cross-task `lv_*`/UI call in one recursive mutex:

```c
static SemaphoreHandle_t lvgl_mux;  // xSemaphoreCreateRecursiveMutex()

// render task:
if (xSemaphoreTakeRecursive(lvgl_mux, portMAX_DELAY)) { lv_timer_handler(); xSemaphoreGiveRecursive(lvgl_mux); }

// any other task touching the UI:
if (xSemaphoreTakeRecursive(lvgl_mux, ...)) { ui_update(...); xSemaphoreGiveRecursive(lvgl_mux); }
```

Also build the UI and create any queues your render task consumes **before** you start that task.

## 4. Draw buffers

- Partial bands in **internal DMA-capable RAM** (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`) flush reliably.
- A full-screen **PSRAM** buffer pushed as one giant `tx_color` can fail (`spi transmit (queue) color failed`) — flush in bands instead, or keep `max_transfer_sz` sane and let esp_lcd chunk.
- Remember the flush-order constraint from the [checklist](Bring-Up-Checklist.md) item 7: the driver's RAMWR/RAMWRC continuation wants full-screen top-to-bottom flushes. Use `disp_drv.full_refresh = 1` with a screen-sized buffer, or switch the driver to `CASET+RASET+RAMWR` per area for arbitrary partial updates.
