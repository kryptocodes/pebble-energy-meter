// Energy Meter - Pebble watchface that looks like an Indian kWh meter.
//
// Six drums show HH MM SS (last one in the accent colour), a disc spins in
// its slot, the CAL LED blinks each second, and the nameplate shows weather,
// steps, kcal, day, date, battery and the phone connection.
#include <pebble.h>

#define NUM_WHEELS 6
#define ROLL_MS 600
#define DISC_MS 990
#define LED_MS 180
// Disc speed per second, scaled by activity "load" (0..100):
// at rest 15 deg/s (1 rev / 24 s), at full load 90 deg/s (1 rev / 4 s).
#define DISC_STEP_MIN (TRIG_MAX_ANGLE / 24)
#define DISC_STEP_MAX (TRIG_MAX_ANGLE / 4)

// ---------------------------------------------------------------- colours
#define COL_BG          GColorWhite
#define COL_PLATE       GColorWhite
#define COL_INK         GColorBlack
#define COL_WHEEL       GColorBlack
#define COL_RED_WHEEL   PBL_IF_COLOR_ELSE(GColorFromHEX(s_cfg.accent), GColorWhite)
#define COL_RED_DIGIT   PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
#define COL_DRUM_EDGE   GColorBlack

// ---------------------------------------------------------------- state
static Window *s_window;
static Layer *s_canvas;
static Layer *s_wheel_layers[NUM_WHEELS];
static Layer *s_disc_layer;
static Layer *s_overlay;
static Animation *s_anim;
static Animation *s_disc_anim;
static AppTimer *s_led_timer;
static bool s_led_on;

static GRect s_content;
static GRect s_wheel_window;
static GPoint s_led_pos;

static int s_digits[NUM_WHEELS] = {-1, -1, -1, -1, -1, -1};
static int s_prev[NUM_WHEELS];
static bool s_rolling[NUM_WHEELS];
static AnimationProgress s_progress;
static int32_t s_disc_angle, s_disc_from;
static int32_t s_disc_step = DISC_STEP_MIN;

// Health
static char s_steps[20] = "-- steps", s_kcal[16] = "-- kcal";

// Weather (fetched by the phone), kept across restarts
#define WEATHER_KEY 2
static char s_weather[24] = "-- \u00B0";
static int s_steps_today = -1, s_steps_last_min = -1, s_spm, s_hr;

static char s_day[8], s_date[12], s_batt[8], s_serial[20];
static uint32_t s_barcode_seed;
static bool s_bt_connected = true;

static GFont s_digit_font, s_kwh_font, s_info_font, s_tiny_font;
static GBitmap *s_isi_bmp;
static bool s_big;

// ---------------------------------------------------------------- settings
// Set from the phone (Clay config page) and kept in persistent storage.
#define SETTINGS_KEY 1
typedef struct {
  char plate[32];        // nameplate line, e.g. "PEBBLE TIME" or "PROPERTY OF TNEB"
  uint8_t ratings_live;  // 1: 240 V | steps | kcal, 0: classic meter ratings
  bool disc_activity;    // disc speed follows steps / heart rate
  bool disc_smooth;      // spin continuously (more battery) vs. step each tick
  bool led_blink;
  bool vibe_disconnect;
  uint32_t accent;       // 0xRRGGBB for seconds wheel, bolt and LED
  bool temp_f;           // weather in Fahrenheit
  uint8_t power_mode;    // POWER_NORMAL / POWER_AUTO / POWER_SAVER
  uint8_t saver_below;   // auto saver threshold, battery %
  bool shake_seconds;    // in saver, a wrist flick shows seconds for a while
} Settings;
static Settings s_cfg;

enum { POWER_NORMAL = 0, POWER_AUTO = 1, POWER_SAVER = 2 };
#define PEEK_MS 30000

// Battery saver state
static bool s_saver;             // saver currently active
static bool s_peek;              // seconds shown for a while after a shake
static AppTimer *s_peek_timer;
static TimeUnits s_tick_unit;    // 0 = not subscribed yet
static bool s_tap_on;
static BatteryChargeState s_batt_state = {.charge_percent = 100};

static bool show_seconds(void) { return !s_saver || s_peek; }

static void settings_defaults(void) {
  memset(&s_cfg, 0, sizeof(s_cfg));
  strncpy(s_cfg.plate, "PEBBLE TIME", sizeof(s_cfg.plate) - 1);
  s_cfg.ratings_live = 1;
  s_cfg.disc_activity = true;
  s_cfg.disc_smooth = true;
  s_cfg.led_blink = true;
  s_cfg.vibe_disconnect = true;
  s_cfg.accent = 0xFF0000;
  s_cfg.power_mode = POWER_AUTO;
  s_cfg.saver_below = 20;
  s_cfg.shake_seconds = true;
}

static void settings_load(void) {
  settings_defaults();
  if (persist_exists(SETTINGS_KEY)) persist_read_data(SETTINGS_KEY, &s_cfg, sizeof(s_cfg));
}

#define COL_ACCENT PBL_IF_COLOR_ELSE(GColorFromHEX(s_cfg.accent), GColorBlack)
static int s_info_fh, s_tiny_fh, s_kwh_fh;
static int s_glyph_center, s_glyph_h, s_text_h, s_pitch;

// ---------------------------------------------------------------- layout
// Everything is designed in pixels on a 144x168 screen and mapped into s_content.
static int X(int v) { return s_content.origin.x + v * s_content.size.w / 144; }
static int Y(int v) { return s_content.origin.y + v * s_content.size.h / 168; }
static GRect R(int x, int y, int w, int h) {
  return GRect(X(x), Y(y), X(x + w) - X(x), Y(y + h) - Y(y));
}

static void upcase(char *s) {
  for (; *s; s++) {
    if (*s >= 'a' && *s <= 'z') *s -= 32;
  }
}

static void draw_text(GContext *ctx, const char *txt, GFont font, GRect r,
                      GTextAlignment align) {
  graphics_draw_text(ctx, txt, font, r, GTextOverflowModeFill, align, NULL);
}

// Draws text vertically centred (by cap height) in r. fh = nominal font size.
static void draw_text_vc(GContext *ctx, const char *txt, GFont font, int fh, GRect r,
                         GTextAlignment align) {
  int cap_top = fh * 32 / 100, cap_h = fh * 72 / 100;
  GRect t = GRect(r.origin.x, r.origin.y + (r.size.h - cap_h) / 2 - cap_top,
                  r.size.w, fh + 8);
  draw_text(ctx, txt, font, t, align);
}

// ---------------------------------------------------------------- symbols
// High-voltage warning: white triangle, black border, red lightning bolt.
static void draw_hazard(GContext *ctx, GPoint c, int size) {
  int h = size * 7 / 8;
  int top = c.y - h / 2 - 1, left = c.x - size / 2;
  GPathInfo tri_info = {
    .num_points = 3,
    .points = (GPoint[]){{c.x, top}, {left, top + h}, {left + size, top + h}},
  };
  GPath *tri = gpath_create(&tri_info);
  graphics_context_set_fill_color(ctx, COL_PLATE);
  gpath_draw_filled(ctx, tri);
  graphics_context_set_stroke_color(ctx, COL_INK);
  graphics_context_set_stroke_width(ctx, size >= 20 ? 2 : 1);
  gpath_draw_outline(ctx, tri);
  graphics_context_set_stroke_width(ctx, 1);
  gpath_destroy(tri);

  // bolt: a thick zig-zag stroke on a 16-unit grid over the triangle's box
#define BP(px, py) GPoint(left + (px) * size / 16, top + (py) * h / 16)
  graphics_context_set_stroke_color(ctx, COL_ACCENT);
  graphics_context_set_stroke_width(ctx, size >= 20 ? 3 : 2);
  graphics_draw_line(ctx, BP(10, 5), BP(6, 10));
  graphics_draw_line(ctx, BP(6, 10), BP(10, 10));
  graphics_draw_line(ctx, BP(10, 10), BP(7, 15));
  graphics_context_set_stroke_width(ctx, 1);
#undef BP
}

// Class II double-insulation mark: a square inside a square.
__attribute__((unused)) static void draw_insulation_mark(GContext *ctx, GPoint c, int size) {
  graphics_context_set_stroke_color(ctx, COL_INK);
  graphics_draw_rect(ctx, GRect(c.x - size / 2, c.y - size / 2, size, size));
  int in = size / 2 - (size >= 12 ? 3 : 2);
  graphics_draw_rect(ctx, GRect(c.x - in, c.y - in, in * 2, in * 2));
}

// ---------------------------------------------------------------- nameplate
static void draw_cell(GContext *ctx, GRect r, const char *txt, GFont font, int fh) {
  graphics_context_set_stroke_color(ctx, COL_INK);
  graphics_draw_rect(ctx, r);
  draw_text_vc(ctx, txt, font, fh, r, GTextAlignmentCenter);
}

static void draw_barcode(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, COL_INK);
  uint32_t seed = s_barcode_seed;
  int end = r.origin.x + r.size.w;
  // guard bars
  graphics_fill_rect(ctx, GRect(r.origin.x, r.origin.y, 1, r.size.h + 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y, 1, r.size.h + 3), 0, GCornerNone);
  int x = r.origin.x + 4;
  while (true) {
    seed = seed * 1103515245u + 12345u;
    int bw = 1 + (int)((seed >> 16) % 3);
    int gap = 1 + (int)((seed >> 20) % 2);
    if (x + bw > end - 4) break;
    graphics_fill_rect(ctx, GRect(x, r.origin.y, bw, r.size.h), 0, GCornerNone);
    x += bw + gap;
  }
  graphics_fill_rect(ctx, GRect(end - 3, r.origin.y, 1, r.size.h + 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(end - 1, r.origin.y, 1, r.size.h + 3), 0, GCornerNone);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_antialiased(ctx, true);

  // ---- full-screen nameplate
  graphics_context_set_fill_color(ctx, COL_PLATE);
#if defined(PBL_ROUND)
  graphics_fill_circle(ctx, grect_center_point(&b), b.size.w / 2);
#else
  graphics_fill_rect(ctx, b, 0, GCornerNone);
#endif

  graphics_context_set_text_color(ctx, COL_INK);

  // ---- "kWh" heading
  draw_text_vc(ctx, "kWh", s_kwh_font, s_kwh_fh, R(30, 2, 84, 20), GTextAlignmentCenter);

  // ---- pulse LED (unlit) + labels
  graphics_context_set_stroke_color(ctx, COL_INK);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, s_led_pos, 4);
  graphics_draw_circle(ctx, s_led_pos, 4);
  draw_text_vc(ctx, s_saver ? "ECO" : "CAL", s_tiny_font, s_tiny_fh,
               GRect(s_led_pos.x + 7, s_led_pos.y - 6, X(50) - s_led_pos.x - 7, 12),
               GTextAlignmentLeft);
  draw_text_vc(ctx, PBL_IF_ROUND_ELSE("CL 1", "CL 1.0"), s_tiny_font, s_tiny_fh,
               GRect(X(PBL_IF_ROUND_ELSE(92, 92)), s_led_pos.y - 6,
                     X(PBL_IF_ROUND_ELSE(117, 120)) - X(92), 12),
               GTextAlignmentRight);
#if !defined(PBL_ROUND)
  draw_insulation_mark(ctx, GPoint(X(45), s_led_pos.y), X(9) - X(0));
#endif
  draw_hazard(ctx, GPoint(X(PBL_IF_ROUND_ELSE(126, 133)), s_led_pos.y), X(18) - X(0));

  // ---- register frame: heavy black outline, drums separated by white gaps
  graphics_context_set_fill_color(ctx, COL_INK);
  graphics_fill_rect(ctx, s_wheel_window, 3, GCornersAll);
  graphics_context_set_fill_color(ctx, COL_PLATE);
  graphics_fill_rect(ctx, grect_inset(s_wheel_window, GEdgeInsets(2)), 0, GCornerNone);

  // multiplier labels under each drum
  static const char *labels[NUM_WHEELS] = {"10h", "1h", "10m", "1m", "10s", "1s"};
  for (int i = 0; i < NUM_WHEELS; i++) {
    GRect f = layer_get_frame(s_wheel_layers[i]);
    draw_text_vc(ctx, labels[i], s_tiny_font, s_tiny_fh,
                 GRect(f.origin.x - 4, s_wheel_window.origin.y + s_wheel_window.size.h + 1,
                       f.size.w + 8, Y(75) - Y(65)),
                 GTextAlignmentCenter);
  }

  // ---- type line
  draw_text_vc(ctx, s_cfg.plate, s_tiny_font, s_tiny_fh, R(3, 90, 138, 10), GTextAlignmentCenter);

  // ---- ratings row
#if !defined(PBL_ROUND)
  {
    int x0 = X(3), x3 = X(141), y = Y(101), h = Y(114) - Y(101);
    int w = x3 - x0;
    if (s_cfg.ratings_live) {
      int xs[4] = {x0, x0 + w * 37 / 100, x0 + w * 70 / 100, x3};
      const char *rt[3] = {s_weather, s_steps, s_kcal};
      for (int i = 0; i < 3; i++) {
        draw_cell(ctx, GRect(xs[i], y, xs[i + 1] - xs[i] + (i < 2 ? 1 : 0), h), rt[i],
                  s_tiny_font, s_tiny_fh);
      }
    } else {
      int xs[5] = {x0, x0 + w * 18 / 100, x0 + w * 39 / 100, x0 + w * 56 / 100, x3};
      const char *rt[4] = {"240 V", "5-30 A", "50 Hz", s_big ? "3200 imp/kWh" : "3200 imp"};
      for (int i = 0; i < 4; i++) {
        draw_cell(ctx, GRect(xs[i], y, xs[i + 1] - xs[i] + (i < 3 ? 1 : 0), h), rt[i],
                  s_tiny_font, s_tiny_fh);
      }
    }
  }
#endif

  // ---- live cells: weekday | date | battery
  {
    int x0 = X(3), x3 = X(141), y = Y(PBL_IF_ROUND_ELSE(102, 116)), h = Y(136) - Y(116);
    int x1 = x0 + (x3 - x0) * 3 / 10;
    int x2 = x0 + (x3 - x0) * 7 / 10;
    draw_cell(ctx, GRect(x0, y, x1 - x0 + 1, h), s_day, s_info_font, s_info_fh);
    draw_cell(ctx, GRect(x1, y, x2 - x1 + 1, h), s_date, s_info_font, s_info_fh);
    draw_cell(ctx, GRect(x2, y, x3 - x2, h), s_batt, s_info_font, s_info_fh);
  }

  // ---- barcode + serial
  GRect bar = R(PBL_IF_ROUND_ELSE(14, 4), PBL_IF_ROUND_ELSE(128, 139), PBL_IF_ROUND_ELSE(68, 74), 13);
  draw_barcode(ctx, bar);

  // ---- ISI certification mark + standard number
  {
    GSize sz = gbitmap_get_bounds(s_isi_bmp).size;
    GRect isi = GRect(X(PBL_IF_ROUND_ELSE(98, 95)) - sz.w / 2, Y(PBL_IF_ROUND_ELSE(125, 136)),
                      sz.w, sz.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_isi_bmp, isi);
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_context_set_text_color(ctx, COL_INK);
    draw_text_vc(ctx, "IS 13779", s_tiny_font, s_tiny_fh,
                 GRect(isi.origin.x - 8, isi.origin.y + isi.size.h, isi.size.w + 16, s_tiny_fh + 2),
                 GTextAlignmentCenter);
  }
  draw_text_vc(ctx, s_serial, s_tiny_font, s_tiny_fh,
               GRect(bar.origin.x, bar.origin.y + bar.size.h + 3, bar.size.w, s_tiny_fh + 2),
               GTextAlignmentCenter);

  // ---- seal sticker = Bluetooth status
  GRect st = R(PBL_IF_ROUND_ELSE(116, 114), PBL_IF_ROUND_ELSE(128, 139), 26, 26);
  graphics_context_set_fill_color(ctx, s_bt_connected ? GColorBlack
                                                     : PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
  graphics_fill_rect(ctx, st, 2, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, st, 2);
  graphics_context_set_text_color(ctx, s_bt_connected ? GColorWhite
                                                      : PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  draw_text_vc(ctx, s_bt_connected ? "BT" : "NC", s_info_font, s_info_fh, st,
               GTextAlignmentCenter);
}

// LED sits on its own layer on top.
static void overlay_update(Layer *layer, GContext *ctx) {
  if (!s_led_on) return;
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, COL_ACCENT);
  graphics_fill_circle(ctx, s_led_pos, 4);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_circle(ctx, s_led_pos, 4);
}

// ---------------------------------------------------------------- drums
static void draw_digit(GContext *ctx, GRect b, int digit, int center, GColor color) {
  char buf[2] = {(char)('0' + (digit + 10) % 10), 0};
  graphics_context_set_text_color(ctx, color);
  draw_text(ctx, buf, s_digit_font,
            GRect(-12, center - s_glyph_center, b.size.w + 24, s_text_h), GTextAlignmentCenter);
}

static void wheel_update(Layer *layer, GContext *ctx) {
  int idx = *(int *)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  bool red = idx == NUM_WHEELS - 1;
  int w = b.size.w, h = b.size.h;

  graphics_context_set_fill_color(ctx, red ? COL_RED_WHEEL : COL_WHEEL);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  int cy = h / 2;
  int d = s_digits[idx] < 0 ? 0 : s_digits[idx];
  GColor fg = red ? COL_RED_DIGIT : GColorWhite;
  if (idx >= 4 && !show_seconds()) {
    // battery saver: seconds drums parked, shown as a dash
    int dw = w / 3, dh = h / 12 < 2 ? 2 : h / 12;
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect((w - dw) / 2, cy - dh / 2, dw, dh), 0, GCornerNone);
  } else if (s_rolling[idx]) {
    int shift = s_pitch * (int)s_progress / ANIMATION_NORMALIZED_MAX;
    int p = s_prev[idx] < 0 ? 0 : s_prev[idx];
    draw_digit(ctx, b, p, cy - shift, fg);
    draw_digit(ctx, b, d, cy + s_pitch - shift, fg);
  } else {
    draw_digit(ctx, b, d, cy, fg);
  }

  // Drum ends: thin dark edges left/right
  graphics_context_set_stroke_color(ctx, COL_DRUM_EDGE);
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(0, h - 1));
  graphics_draw_line(ctx, GPoint(w - 1, 0), GPoint(w - 1, h - 1));
#if !defined(PBL_COLOR)
  if (red) {  // the "red" drum is white on B/W, so outline it
    graphics_context_set_stroke_color(ctx, COL_INK);
    graphics_draw_rect(ctx, b);
  }
#endif
}

// ---------------------------------------------------------------- disc
static void disc_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 3, GCornersAll);

  int band_h = b.size.h / 2;
  if (band_h < 4) band_h = 4;
  GRect band = GRect(3, (b.size.h - band_h) / 2, b.size.w - 6, band_h);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, band, 0, GCornerNone);

  int cx = b.size.w / 2;
  int r = band.size.w / 2 - 1;

  // Knurled ribs sweep across as it turns (front half only)
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < 24; i++) {
    int32_t a = s_disc_angle + i * (TRIG_MAX_ANGLE / 24);
    if (cos_lookup(a) <= TRIG_MAX_RATIO / 8) continue;
    int x = cx + (int)(sin_lookup(a) * r / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, GPoint(x, band.origin.y + 2),
                       GPoint(x, band.origin.y + band.size.h - 3));
  }

  // The black index mark, wider when facing us
  int32_t c = cos_lookup(s_disc_angle);
  if (c > 0) {
    int x = cx + (int)(sin_lookup(s_disc_angle) * r / TRIG_MAX_RATIO);
    int w = 2 + (int)(10 * c / TRIG_MAX_RATIO);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(x - w / 2, band.origin.y, w, band.size.h), 0, GCornerNone);
  }
}

// ---------------------------------------------------------------- animation
static void anim_update(Animation *a, const AnimationProgress p) {
  s_progress = p;
  for (int i = 0; i < NUM_WHEELS; i++) {
    if (s_rolling[i]) layer_mark_dirty(s_wheel_layers[i]);
  }
}

static void anim_teardown(Animation *a) {
  for (int i = 0; i < NUM_WHEELS; i++) {
    if (s_rolling[i]) {
      s_rolling[i] = false;
      layer_mark_dirty(s_wheel_layers[i]);
    }
  }
  s_anim = NULL;
}

static const AnimationImplementation s_anim_impl = {
  .update = anim_update,
  .teardown = anim_teardown,
};

// The disc turns at a constant speed through the whole second, so it never
// stops between ticks.
static void disc_anim_update(Animation *a, const AnimationProgress p) {
  s_disc_angle = s_disc_from + (int32_t)((int64_t)s_disc_step * p / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_disc_layer);
}

static void disc_anim_teardown(Animation *a) {
  s_disc_angle = (s_disc_from + s_disc_step) % TRIG_MAX_ANGLE;
  s_disc_anim = NULL;
  layer_mark_dirty(s_disc_layer);
}

static const AnimationImplementation s_disc_anim_impl = {
  .update = disc_anim_update,
  .teardown = disc_anim_teardown,
};

static void led_off(void *data) {
  s_led_timer = NULL;
  s_led_on = false;
  layer_mark_dirty(s_overlay);
}

static void led_pulse(void) {
  s_led_on = true;
  layer_mark_dirty(s_overlay);
  if (s_led_timer) app_timer_cancel(s_led_timer);
  s_led_timer = app_timer_register(LED_MS, led_off, NULL);
}

// ---------------------------------------------------------------- data
static void update_date(struct tm *t) {
  strftime(s_day, sizeof(s_day), "%a", t);
  strftime(s_date, sizeof(s_date), "%d %b", t);
  strftime(s_serial, sizeof(s_serial), "No. %Y%m%d", t);
  upcase(s_day);
  upcase(s_date);
  s_barcode_seed = (uint32_t)(t->tm_year * 400 + t->tm_yday) * 2654435761u;
  layer_mark_dirty(s_canvas);
}

static void update_time(struct tm *t, bool animate) {
  int h = t->tm_hour;
  if (!clock_is_24h_style()) {
    h %= 12;
    if (h == 0) h = 12;
  }
  int v[NUM_WHEELS] = {h / 10, h % 10, t->tm_min / 10, t->tm_min % 10,
                       t->tm_sec / 10, t->tm_sec % 10};
  bool secs = show_seconds();

  if (s_anim) animation_unschedule(s_anim);  // teardown finalises the old roll

  for (int i = 0; i < NUM_WHEELS; i++) {
    if (i >= 4 && !secs) continue;  // parked in saver mode
    if (v[i] != s_digits[i]) {
      s_prev[i] = s_digits[i];
      s_digits[i] = v[i];
      s_rolling[i] = animate && s_prev[i] >= 0;
      layer_mark_dirty(s_wheel_layers[i]);
    }
  }

  if (s_disc_anim) animation_unschedule(s_disc_anim);  // lands exactly on the step
  s_disc_from = s_disc_angle;
  if (animate) {
    s_disc_anim = animation_create();
    // Continuous spin redraws the screen ~30 times a second; in saver (or with
    // smooth off) the disc only makes a short eased step per tick.
    bool smooth = s_cfg.disc_smooth && !s_saver;
    animation_set_duration(s_disc_anim, smooth ? DISC_MS : ROLL_MS);
    animation_set_curve(s_disc_anim, smooth ? AnimationCurveLinear : AnimationCurveEaseInOut);
    animation_set_implementation(s_disc_anim, &s_disc_anim_impl);
    animation_schedule(s_disc_anim);

    s_progress = 0;
    s_anim = animation_create();
    animation_set_duration(s_anim, ROLL_MS);
    animation_set_curve(s_anim, AnimationCurveEaseInOut);
    animation_set_implementation(s_anim, &s_anim_impl);
    animation_schedule(s_anim);
    if (s_cfg.led_blink && !s_saver) led_pulse();
  }
}

// ---------------------------------------------------------------- weather
static void request_weather(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_uint8(out, MESSAGE_KEY_RequestWeather, 1);
  dict_write_uint8(out, MESSAGE_KEY_TempUnit, s_cfg.temp_f ? 1 : 0);
  app_message_outbox_send();
}

// ---------------------------------------------------------------- health
static void fmt_thousands(char *buf, size_t n, int v, const char *unit) {
  if (v >= 1000) snprintf(buf, n, "%d,%03d %s", v / 1000, v % 1000, unit);
  else snprintf(buf, n, "%d %s", v, unit);
}

// Activity load 0..100 from step rate (100 spm = full) and heart rate
// (60 bpm = 0, 150 bpm = full); whichever is higher sets the disc speed.
static void update_load(void) {
  if (!s_cfg.disc_activity) {
    s_disc_step = TRIG_MAX_ANGLE / 12;  // fixed: 1 rev / 12 s
    return;
  }
  int load = s_spm > 100 ? 100 : s_spm;
  if (s_hr > 60) {
    int hl = (s_hr - 60) * 100 / 90;
    if (hl > 100) hl = 100;
    if (hl > load) load = hl;
  }
  s_disc_step = DISC_STEP_MIN + (DISC_STEP_MAX - DISC_STEP_MIN) * load / 100;
}

static void update_health(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today(), end = time(NULL);
  if (health_service_metric_accessible(HealthMetricStepCount, start, end) & HealthServiceAccessibilityMaskAvailable) {
    s_steps_today = (int)health_service_sum_today(HealthMetricStepCount);
    fmt_thousands(s_steps, sizeof(s_steps), s_steps_today, "steps");
  }
  if (health_service_metric_accessible(HealthMetricActiveKCalories, start, end) & HealthServiceAccessibilityMaskAvailable) {
    fmt_thousands(s_kcal, sizeof(s_kcal), (int)health_service_sum_today(HealthMetricActiveKCalories), "kcal");
  }
  if (health_service_metric_accessible(HealthMetricHeartRateBPM, end, end) & HealthServiceAccessibilityMaskAvailable) {
    s_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  }
  update_load();
  layer_mark_dirty(s_canvas);
#endif
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventHeartRateUpdate ||
      event == HealthEventSignificantUpdate) {
    update_health();
  }
}
#endif

// Called once a minute: steps per minute from the change in today's count.
static void update_step_rate(void) {
  if (s_steps_today >= 0 && s_steps_last_min >= 0 && s_steps_today >= s_steps_last_min) {
    s_spm = s_steps_today - s_steps_last_min;
  } else {
    s_spm = 0;
  }
  s_steps_last_min = s_steps_today;
  update_load();
}

static void tick_handler(struct tm *t, TimeUnits changed) {
  if (changed & MINUTE_UNIT) update_step_rate();
  if ((changed & MINUTE_UNIT) && t->tm_min % (s_saver ? 60 : 30) == 0) request_weather();
  update_time(t, true);
  if (changed & DAY_UNIT) update_date(t);
}

// ---------------------------------------------------------------- power
static void tap_handler(AccelAxisType axis, int32_t direction);

static void set_tick_unit(TimeUnits unit) {
  if (unit == s_tick_unit) return;
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(unit, tick_handler);
  s_tick_unit = unit;
  time_t now = time(NULL);
  update_time(localtime(&now), false);
  for (int i = 4; i < NUM_WHEELS; i++) layer_mark_dirty(s_wheel_layers[i]);
}

// Works out whether the saver should be on and sets the tick rate, the
// shake listener and the plate to match.
static void power_apply(void) {
  bool saver = s_cfg.power_mode == POWER_SAVER ||
               (s_cfg.power_mode == POWER_AUTO && !s_batt_state.is_charging &&
                !s_batt_state.is_plugged && s_batt_state.charge_percent <= s_cfg.saver_below);
  if (!saver && s_peek) {
    s_peek = false;
    if (s_peek_timer) app_timer_cancel(s_peek_timer);
    s_peek_timer = NULL;
  }
  if (saver && s_disc_anim) animation_unschedule(s_disc_anim);
  s_saver = saver;
  set_tick_unit(show_seconds() ? SECOND_UNIT : MINUTE_UNIT);

  bool tap = saver && s_cfg.shake_seconds;
  if (tap != s_tap_on) {
    if (tap) accel_tap_service_subscribe(tap_handler);
    else accel_tap_service_unsubscribe();
    s_tap_on = tap;
  }
  layer_mark_dirty(s_canvas);
}

static void peek_end(void *data) {
  s_peek_timer = NULL;
  s_peek = false;
  power_apply();
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  if (!s_saver) return;
  s_peek = true;
  if (s_peek_timer) app_timer_reschedule(s_peek_timer, PEEK_MS);
  else s_peek_timer = app_timer_register(PEEK_MS, peek_end, NULL);
  power_apply();
}

static void battery_handler(BatteryChargeState st) {
  s_batt_state = st;
  snprintf(s_batt, sizeof(s_batt), "%d%%", st.charge_percent);
  power_apply();
}

static void bt_handler(bool connected) {
  if (s_bt_connected && !connected && s_cfg.vibe_disconnect) vibes_double_pulse();
  s_bt_connected = connected;
  layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------- window
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

#if defined(PBL_ROUND)
  // Squeeze the 144x168 design into the round screen
  s_content = GRect(b.size.w * 28 / 180, b.size.h * 18 / 180,
                    b.size.w * 124 / 180, b.size.h * 146 / 180);
#else
  s_content = b;
#endif

  bool big = b.size.w >= 200;
  s_big = big;

  // drum digits: Arimo Bold, one size per platform
  int drum_size;
#if defined(PBL_PLATFORM_EMERY)
  drum_size = 34;
  s_digit_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DRUM_34));
#elif defined(PBL_PLATFORM_CHALK)
  drum_size = 20;
  s_digit_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DRUM_20));
#else
  drum_size = 24;
  s_digit_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DRUM_24));
#endif
  s_glyph_h = drum_size * 72 / 100;
  s_glyph_center = drum_size * 56 / 100;
  s_text_h = drum_size * 3 / 2;

  s_kwh_font = fonts_get_system_font(big ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD);
  s_info_font = fonts_get_system_font(big ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  s_tiny_font = fonts_get_system_font(big ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_09);
  s_isi_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ISI);
  s_kwh_fh = big ? 28 : 24;
  s_info_fh = big ? 24 : 18;
  s_tiny_fh = big ? 14 : 9;

  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);

  s_led_pos = GPoint(X(PBL_IF_ROUND_ELSE(16, 9)), Y(12));

  // Drums
  s_wheel_window = R(3, 24, 138, 38);
  int pad = 3, gap = 2;
  int inner_w = s_wheel_window.size.w - 2 * pad;
  int cw = (inner_w - (NUM_WHEELS - 1) * gap) / NUM_WHEELS;
  int ch = s_wheel_window.size.h - 2 * pad;
  int x0 = s_wheel_window.origin.x + pad +
           (inner_w - (cw * NUM_WHEELS + gap * (NUM_WHEELS - 1))) / 2;
  s_pitch = ch / 2 + s_glyph_h / 2 + 3;  // neighbours stay hidden
  for (int i = 0; i < NUM_WHEELS; i++) {
    GRect f = GRect(x0 + i * (cw + gap), s_wheel_window.origin.y + pad, cw, ch);
    s_wheel_layers[i] = layer_create_with_data(f, sizeof(int));
    *(int *)layer_get_data(s_wheel_layers[i]) = i;
    layer_set_update_proc(s_wheel_layers[i], wheel_update);
    layer_add_child(root, s_wheel_layers[i]);
  }

  // Disc slot
  s_disc_layer = layer_create(R(4, 77, 136, 11));
  layer_set_update_proc(s_disc_layer, disc_update);
  layer_add_child(root, s_disc_layer);

  s_overlay = layer_create(b);
  layer_set_update_proc(s_overlay, overlay_update);
  layer_add_child(root, s_overlay);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t, false);
  update_date(t);
  battery_handler(battery_state_service_peek());
  update_health();
  s_steps_last_min = s_steps_today;
  if (persist_exists(WEATHER_KEY)) persist_read_string(WEATHER_KEY, s_weather, sizeof(s_weather));
  s_bt_connected = connection_service_peek_pebble_app_connection();
}

static void window_unload(Window *window) {
  if (s_anim) animation_unschedule(s_anim);
  if (s_disc_anim) animation_unschedule(s_disc_anim);
  if (s_led_timer) app_timer_cancel(s_led_timer);
  for (int i = 0; i < NUM_WHEELS; i++) layer_destroy(s_wheel_layers[i]);
  layer_destroy(s_disc_layer);
  layer_destroy(s_overlay);
  layer_destroy(s_canvas);
  fonts_unload_custom_font(s_digit_font);
  gbitmap_destroy(s_isi_bmp);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_PlateText))) {
    strncpy(s_cfg.plate, t->value->cstring, sizeof(s_cfg.plate) - 1);
    s_cfg.plate[sizeof(s_cfg.plate) - 1] = 0;
    upcase(s_cfg.plate);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_RatingsMode))) {
    s_cfg.ratings_live = (t->type == TUPLE_CSTRING) ? (strcmp(t->value->cstring, "live") == 0)
                                                    : (t->value->int32 != 0);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DiscActivity))) s_cfg.disc_activity = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_DiscSmooth))) s_cfg.disc_smooth = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_LedBlink))) s_cfg.led_blink = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_VibeDisconnect))) s_cfg.vibe_disconnect = t->value->int32 != 0;
  if ((t = dict_find(iter, MESSAGE_KEY_AccentColor))) s_cfg.accent = (uint32_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_PowerMode))) {
    if (t->type == TUPLE_CSTRING) {
      const char *m = t->value->cstring;
      s_cfg.power_mode = strcmp(m, "saver") == 0 ? POWER_SAVER
                       : strcmp(m, "normal") == 0 ? POWER_NORMAL : POWER_AUTO;
    } else {
      s_cfg.power_mode = (uint8_t)t->value->int32;
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SaverBelow))) s_cfg.saver_below = (uint8_t)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_ShakeSeconds))) s_cfg.shake_seconds = t->value->int32 != 0;
  bool unit_changed = false;
  if ((t = dict_find(iter, MESSAGE_KEY_TempUnit))) {
    bool f = (t->type == TUPLE_CSTRING) ? (strcmp(t->value->cstring, "F") == 0) : (t->value->int32 != 0);
    unit_changed = f != s_cfg.temp_f;
    s_cfg.temp_f = f;
  }
  Tuple *temp = dict_find(iter, MESSAGE_KEY_Temperature);
  Tuple *cond = dict_find(iter, MESSAGE_KEY_Conditions);
  if (temp && cond) {
    snprintf(s_weather, sizeof(s_weather), "%d\u00B0%c %s", (int)temp->value->int32,
             s_cfg.temp_f ? 'F' : 'C', cond->value->cstring);
    persist_write_string(WEATHER_KEY, s_weather);
    layer_mark_dirty(s_canvas);
    return;  // weather-only message
  }
  persist_write_data(SETTINGS_KEY, &s_cfg, sizeof(s_cfg));
  if (unit_changed) request_weather();
  update_load();
  power_apply();
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void init(void) {
  settings_load();
  s_window = window_create();
  window_set_background_color(s_window, COL_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);

  // tick rate is chosen by power_apply() (first called from window_load)
  battery_state_service_subscribe(battery_handler);
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = bt_handler,
  });
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_tap_on) accel_tap_service_unsubscribe();
  if (s_peek_timer) app_timer_cancel(s_peek_timer);
  battery_state_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
