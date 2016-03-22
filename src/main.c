#include <pebble.h>

#define COLORS       PBL_IF_COLOR_ELSE(true, false)
#define ANTIALIASING true

#define HAND_MARGIN  14
#define FINAL_RADIUS 58

#define ANIMATION_DURATION 800
#define ANIMATION_DELAY    600

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *s_main_window;

static TextLayer *s_time_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_connection_layer;

static Layer *s_canvas_layer;



static GPoint s_center;
static Time s_last_time, s_anim_time;
static struct tm *s_tm;
static int s_radius = 0;
static int s_anim_hours_60 = 0;
static int s_color_channels[3];
static bool s_animating = false;
static bool s_setting = false;

/*************************** AnimationImplementation **************************/
// アニメーション開始時の処理（FLGを立てる）
static void animation_started(Animation *anim, void *context) {
  s_animating = true;
}
// アニメーション終了時の処理（FLGを落とす）
static void animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;
}

// アニメーション処理
static void animate(int duration, int delay, AnimationImplementation *implementation, bool handlers) {
  // アニメーションを生成
  Animation *anim = animation_create();
  // 動作時間を設定
  animation_set_duration(anim, duration);
  // 遅延時間を設定
  animation_set_delay(anim, delay);
  // カーブをセット
  animation_set_curve(anim, AnimationCurveEaseInOut);
  // アニメーション実装(引数から実装）
  animation_set_implementation(anim, implementation);
  // アニメーション開始と終了のイベントを割り当てる
  if(handlers) {
    animation_set_handlers(anim, (AnimationHandlers) {
      .started = animation_started,
      .stopped = animation_stopped
    }, NULL);
  }
  // アニメーションスケジュール登録
  animation_schedule(anim);
}

/************************************ UI **************************************/

// 定周期割り込み
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  // Store time
  // 時間を保存
  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;


  // 色を設定（乱数？）
  for(int i = 0; i < 3; i++) {
    s_color_channels[i] = rand() % 256;
  }
  
  // Redraw（再描画）
  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
  if(s_setting){
    
    // システムが後で使ったのでstaticでないといけない。
    static char s_time_text[] = "00:00";

    strftime(s_time_text, sizeof(s_time_text), "%T", tick_time);
    text_layer_set_text(s_time_layer, s_time_text);
  }
  
}


static int hours_to_minutes(int hours_out_of_12) {
  return (int)(float)(((float)hours_out_of_12 / 12.0F) * 60.0F);
}

// 描画更新処理（レイヤー、グラフィックコンテキスト）
static void update_proc(Layer *layer, GContext *ctx) {
  // Color background?
  GRect bounds = layer_get_bounds(layer);
  if(COLORS) {
    graphics_context_set_fill_color(ctx, GColorFromRGB(s_color_channels[0], s_color_channels[1], s_color_channels[2]));
  } else {
    // 塗り潰しを灰色に設定
    graphics_context_set_fill_color(ctx, GColorDarkGray);
  }
  // 塗り潰し範囲をキャンバスレイヤーに設定
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // 線の色を黒に設定
  graphics_context_set_stroke_color(ctx, GColorBlack);
  // 線の幅を４に設定
  graphics_context_set_stroke_width(ctx, 2);

  // アンチエリアスに設定？
  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // White clockface
  // 時計の表面を白に設定
  graphics_context_set_fill_color(ctx, GColorBlack);
  // 円を塗り潰し
  graphics_fill_circle(ctx, s_center, s_radius+HAND_MARGIN);
  // 時計の表面を白に設定
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  // 円を塗り潰し
  graphics_fill_circle(ctx, s_center, s_radius);
  // 時計の表面を白に設定
  graphics_context_set_fill_color(ctx, GColorWhite);
  if(s_radius >  HAND_MARGIN) {
    // 円を塗り潰し
    graphics_fill_circle(ctx, s_center, s_radius-HAND_MARGIN);
    // Draw outline
    // 円のふちを描画
    graphics_draw_circle(ctx, s_center, s_radius-HAND_MARGIN-1);
    // 線の色を黒に設定
    graphics_context_set_stroke_color(ctx, GColorWhite);
        // 円のふちを描画
    graphics_draw_circle(ctx, s_center, s_radius+HAND_MARGIN+1);

    }
  // 時計の表面を白に設定
  graphics_context_set_fill_color(ctx, GColorBlack);
  // 円を塗り潰し
  graphics_fill_circle(ctx, s_center, 2);

  
  
  // Don't use current time while animating
  // アニメーション中で有れば、アニメ用時間、なければ現在時間
  Time mode_time = (s_animating) ? s_anim_time : s_last_time;

  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if(s_animating) {
    // Hours out of 60 for smoothness
    // 時間の針の角度を分で微調整
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  // 分針の描画座標
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(1+s_radius + (HAND_MARGIN /2) ) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(1+s_radius + (HAND_MARGIN/2) )/ TRIG_MAX_RATIO) + s_center.y,
  };
  // 時針の描画座標
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(1+s_radius - (HAND_MARGIN/2) ) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(1+s_radius - (HAND_MARGIN/2) ) / TRIG_MAX_RATIO) + s_center.y,
  };

  // Draw hands with positive length only
  // 時針を描画
  if(s_radius >  HAND_MARGIN/2) {
    // 線の色を黒に設定
    graphics_context_set_stroke_color(ctx, GColorBlack);
    // 線の幅を４に設定
    graphics_context_set_stroke_width(ctx, 1); 
    // 線を引く
    graphics_draw_line(ctx, s_center, hour_hand);
    
    // 時計の表面を白に設定
    graphics_context_set_fill_color(ctx, GColorWhite);
    // 円を塗り潰し
    graphics_fill_circle(ctx, hour_hand, (int32_t)(HAND_MARGIN / 2));
    // 線の色を黒に設定
    graphics_context_set_stroke_color(ctx, GColorBlack);
    // 線の幅を４に設定
    graphics_context_set_stroke_width(ctx, 2);
    // 
    graphics_draw_circle(ctx, hour_hand, (int32_t)(HAND_MARGIN / 2));
  }
  // 分針を描画
  if(s_radius > 0) {
    // 線の色を黒に設定
    graphics_context_set_stroke_color(ctx, GColorBlack);
    // 線の幅を４に設定
    graphics_context_set_stroke_width(ctx, 1); 
    // 線を引く
    graphics_draw_line(ctx, s_center, minute_hand);
    // 時計の表面を白に設定
    graphics_context_set_fill_color(ctx, GColorWhite);
    // 円を塗り潰し
    graphics_fill_circle(ctx, minute_hand, (int32_t)(HAND_MARGIN / 2));
    // 線の色を黒に設定
    graphics_context_set_stroke_color(ctx, GColorBlack);
    // 線の幅を1に設定
    graphics_context_set_stroke_width(ctx, 2);
    // 
    graphics_draw_circle(ctx, minute_hand, (int32_t)(HAND_MARGIN / 2));

  }

}


static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100%";

  if (charge_state.is_charging) {
    snprintf(battery_text, sizeof(battery_text), "CHG");
  } else {
    snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, battery_text);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits changed) {
  // Needs to be static because it's used by the system later.
  // システムが後で使ったのでstaticでないといけない。
  static char s_time_text[] = "00:00";

  strftime(s_time_text, sizeof(s_time_text), "%T", tick_time);
  text_layer_set_text(s_time_layer, s_time_text);

}

static void handle_bluetooth(bool connected) {
  text_layer_set_text(s_connection_layer, connected ? "OK" : "NG");
}


static void window_load(Window *window) {
  // Windowレイヤー設定
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // 中心を取得
  s_center = grect_center_point(&window_bounds);

  // キャンバスレイヤー生成
  s_canvas_layer = layer_create(window_bounds);
  // キャンバスレイヤーの更新時の処理を設定（CANVASの変更で呼ばれる）
  layer_set_update_proc(s_canvas_layer, update_proc);
  // Windowレイヤーにキャンバスレイヤーを追加
  layer_add_child(window_layer, s_canvas_layer);
  
  s_time_layer = text_layer_create(GRect(45, 50, window_bounds.size.w - 90, 34));
  text_layer_set_text_color(s_time_layer, GColorBlack);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  s_connection_layer = text_layer_create(GRect((int)(window_bounds.size.w / 2) - 25,window_bounds.size.h -84, 40, 30));
  text_layer_set_text_color(s_connection_layer, GColorBlack);
  text_layer_set_background_color(s_connection_layer, GColorClear);
  text_layer_set_font(s_connection_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_connection_layer, GTextAlignmentLeft);
  handle_bluetooth(connection_service_peek_pebble_app_connection());

  s_battery_layer = text_layer_create(GRect((int)(window_bounds.size.w / 2) + 1, window_bounds.size.h -84, window_bounds.size.w,30));
  text_layer_set_text_color(s_battery_layer, GColorBlack);
  text_layer_set_background_color(s_battery_layer, GColorClear);
  text_layer_set_font(s_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_battery_layer, GTextAlignmentLeft);
  text_layer_set_text(s_battery_layer, "100%");

  // Ensures time is displayed immediately (will break if NULL tick event accessed).
  // (This is why it's a good idea to have a separate routine to do the update itself.)
  //time_t now = time(NULL);
  //struct tm *current_time = localtime(&now);
  //handle_second_tick(current_time,MINUTE_UNIT);

  //tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
  
  battery_state_service_subscribe(handle_battery);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = handle_bluetooth
  });
 
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_connection_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_battery_layer));

  handle_battery(battery_state_service_peek());

  s_setting = true;
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_connection_layer);
  text_layer_destroy(s_battery_layer);

  
  // キャンバスレイヤーを開放
  layer_destroy(s_canvas_layer);
}

/*********************************** App **************************************/

static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(float)(((float)dist_normalized / (float)ANIMATION_NORMALIZED_MAX) * (float)max);
}

// アニメーションの更新（radius）終わりを設定？
static void radius_update(Animation *anim, AnimationProgress dist_normalized) {
  s_radius = anim_percentage(dist_normalized, FINAL_RADIUS);

  layer_mark_dirty(s_canvas_layer);
}

// アニメーションの更新（hands）終わりを設定？
static void hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_anim_time.hours = anim_percentage(dist_normalized, hours_to_minutes(s_last_time.hours));
  s_anim_time.minutes = anim_percentage(dist_normalized, s_last_time.minutes);

  layer_mark_dirty(s_canvas_layer);
}

// 初期処理（起動時のアニメーション）
static void init() {
  // 時間を取得？
  srand(time(NULL));

  // 
  time_t t = time(NULL);
  // 現在時刻を取得
  struct tm *time_now = localtime(&t);
  // タイマーを生成
  tick_handler(time_now, MINUTE_UNIT);

  // 画面を生成
  s_main_window = window_create();
  // 画面にイベントを割り当てる
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_main_window, true);

  // タイマー割り込みサービス起動
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
 
  // Prepare animations
  // アニメーション設定　背景描画（更新処理割り当て）
  AnimationImplementation radius_impl = {
    .update = radius_update
  };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &radius_impl, false);

  // アニメーション設定　針を現在時刻まで移動（更新処理割り当て）
  AnimationImplementation hands_impl = {
    .update = hands_update
  };
  animate(2 * ANIMATION_DURATION, ANIMATION_DELAY, &hands_impl, true);
  
  // タイマー割り込みサービス起動
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

}

static void deinit() {
  window_destroy(s_main_window);
}
// メイン処理
int main() {
  // 初期処理（起動時アニメーション）
  init();
  // 本体（割り込み待ち）
  app_event_loop();
  // 後処理
  deinit();
}