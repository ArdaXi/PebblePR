#include "pebble.h"

#define NUM_MENU_SECTIONS 1
#define NUM_MENU_ICONS 3
#define NUM_MENU_ITEMS 36
#define MENU_HEADER "Parking lots"
  
enum {
  LOT_KEY_TYPE = 0x0,
  LOT_KEY_ID = 0x1,
  LOT_KEY_FREE = 0x2,
  LOT_KEY_TOTAL = 0x3,
  LOT_KEY_NAME = 0x4,
};

enum {
  TYPE_KEY_ALLOC = 0x0,
  TYPE_KEY_INIT = 0x1,
  TYPE_KEY_STATUS = 0x2,
};
  
typedef struct {
  char *free;
  char *total;
  char *name;
  size_t size;
} lot;

static void populate_lots();
static void destroy_lots();

static uint16_t num_lots = NUM_MENU_ITEMS;
static lot **lots = NULL;
static bool populated = false;

static Window *s_main_window;
static MenuLayer *s_menu_layer;

static Window *s_card_window;
static TextLayer *s_free_layer, *s_total_layer, *s_name_layer;
static lot *s_card_lot;

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return NUM_MENU_SECTIONS;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return num_lots;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  menu_cell_basic_header_draw(ctx, cell_layer, MENU_HEADER);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  lot* current_lot = lots[cell_index->row];
  char buf[10]; // Assuming parking lot capacity will not exceed 9999
  snprintf(buf, 10, "%s/%s", current_lot->free, current_lot->total);
  menu_cell_basic_draw(ctx, cell_layer, current_lot->name, buf, NULL);
}

static bool card_shown = false;

static const char* full = "Full";

static void update() {
  Layer *menu_layer = window_get_root_layer(s_main_window);    
  layer_mark_dirty(menu_layer);
  
  if(!card_shown)
    return;
  
  if(s_card_lot->free[0] == '0') {
    text_layer_set_text(s_free_layer, full);
  } else {
    text_layer_set_text(s_free_layer, s_card_lot->free);
  }
  text_layer_set_text(s_total_layer, s_card_lot->total);
  text_layer_set_text(s_name_layer, s_card_lot->name);
  
  /* Might not be necessary:
  Layer *card_layer = window_get_root_layer(s_card_window);
  layer_mark_dirty(card_layer);
  */
}

static lot *lot_new(const char* name, const char* total){
  size_t name_size = strlen(name);
  size_t num_size = strlen(total);
  lot *ret = malloc(sizeof(lot));
  *ret = (lot){malloc(num_size), malloc(num_size), malloc(name_size), num_size};
  strncpy(ret->name, name, name_size);
  strncpy(ret->total, total, num_size);
  strncpy(ret->free, "0", num_size);
  return ret;
}

static void lot_destroy(lot *current_lot) {
  free(current_lot->free);
  free(current_lot->total);
  free(current_lot->name);
  free(current_lot);
}

static void in_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *type_tuple = dict_find(iter, LOT_KEY_TYPE);
  Tuple *id_tuple = dict_find(iter, LOT_KEY_ID);
  Tuple *free_tuple = dict_find(iter, LOT_KEY_FREE);
  Tuple *total_tuple = dict_find(iter, LOT_KEY_TOTAL);
  Tuple *name_tuple = dict_find(iter, LOT_KEY_NAME);
  
  uint8_t type = type_tuple->value->uint8;
  
  if (type == TYPE_KEY_ALLOC) {
    num_lots = total_tuple->value->uint8;
    lots = malloc(sizeof(*lots) * num_lots);
    return;
  }
  
  if (!lots) {
    // log error
    return;
  }
  
  uint8_t id = id_tuple->value->uint8;
  if(id >= num_lots) {
    // log error
    return;
  }
  
  if(!lots[id]) {
    if (type_tuple->value->uint8 != TYPE_KEY_INIT) {
      DictionaryIterator *iter;
      app_message_outbox_begin(&iter);
      dict_write_uint8(iter, LOT_KEY_TYPE, TYPE_KEY_INIT);
      dict_write_uint8(iter, LOT_KEY_ID, id);
      dict_write_end(iter);
      app_message_outbox_send();
      return;
    }
    lots[id] = lot_new(name_tuple->value->cstring, total_tuple->value->cstring);
  }
  
  strncpy(lots[id]->free, free_tuple->value->cstring, lots[id]->size);
  update();
}

static void card_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  
  bounds.origin.x = 5;
  bounds.origin.y = 0;
  bounds.size.w -= 5;
  bounds.size.h -= 110;
  
  s_free_layer = text_layer_create(bounds);
  text_layer_set_font(s_free_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
 
  bounds.origin.y += bounds.size.h;
  bounds.size.h -= 14;
  
  s_total_layer = text_layer_create(bounds);
  text_layer_set_font(s_total_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
  
  bounds.origin.y += bounds.size.h;
  bounds.size.h += 24;
  
  s_name_layer = text_layer_create(bounds);
  text_layer_set_font(s_name_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_overflow_mode(s_name_layer, GTextOverflowModeWordWrap);
  
  layer_add_child(window_layer, text_layer_get_layer(s_free_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_total_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_name_layer));
  
  card_shown = true;
  
  update();
}

static void card_window_unload(Window *window) {
  text_layer_destroy(s_free_layer);
  text_layer_destroy(s_total_layer);
  card_shown = false;
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  s_card_lot = lots[cell_index->row];
  window_stack_push(s_card_window, true);
}

static void main_window_load(Window *window) {
  // Now we prepare to initialize the menu layer
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);

  // Create the menu layer
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
  });

  // Bind the menu layer's click config provider to the window for interactivity
  menu_layer_set_click_config_onto_window(s_menu_layer, window);

  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void main_window_unload(Window *window) {
  // Destroy the menu layer
  menu_layer_destroy(s_menu_layer);
}

static void init() {
  populate_lots();
  
  app_message_register_inbox_received(in_received_handler);
  
  app_message_open(80, 64);
  
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  
  s_card_window = window_create();
  window_set_window_handlers(s_card_window, (WindowHandlers) {
    .load = card_window_load,
    .unload = card_window_unload,
  });
  
  window_stack_push(s_main_window, true);
}

static void deinit() {
  window_destroy(s_main_window);
  window_destroy(s_card_window);
  destroy_lots();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

static void populate_lots() {
  lots = malloc(sizeof(*lots) * num_lots);
  lots[0] = lot_new("CE-P12 Markenhoven", "1500");
  lots[1] = lot_new("ZO-P23 Bijlmerdreef", "1500");
  lots[2] = lot_new("ZO-P05 Villa ArenA", "1500");
  lots[3] = lot_new("CE-P09 Bijenkorf", "1500");
  lots[4] = lot_new("CE-P04 Amsterdam Centraal", "1500");
  lots[5] = lot_new("ZO-P22 Bijlmerdreef", "1500");
  lots[6] = lot_new("CE-P11 Waterlooplein", "1500");
  lots[7] = lot_new("ZO-P21 Bijlmerdreef", "1500");
  lots[8] = lot_new("ZO-P24 Bijlmerdreef", "1500");
  lots[9] = lot_new("ZO-P04 Villa ArenA", "1500");
  lots[10] = lot_new("ZD-P3 VU campus", "1500");
  lots[11] = lot_new("ZD-P2 VUmc (ACTA)", "1500");
  lots[12] = lot_new("ZD-P1 VUmc (westflank)", "1500");
  lots[13] = lot_new("CE P+R Zeeburg 2", "1500");
  lots[14] = lot_new("CE-P08 De Kolk", "1500");
  lots[15] = lot_new("CE P+R Zeeburg 1", "1500");
  lots[16] = lot_new("CE-P03 Piet Hein", "1500");
  lots[17] = lot_new("ZO-P01 ArenA / P+R", "1500");
  lots[18] = lot_new("ZO-P01 ArenA", "1500");
  lots[19] = lot_new("ZO-P18 HES/ROC", "1500");
  lots[20] = lot_new("CE P+R Bos en Lommer", "1500");
  lots[21] = lot_new("CE-P07 Museumplein", "1500");
  lots[22] = lot_new("CE-P Olympisch Stadion", "1500");
  lots[23] = lot_new("CE-P02 P+R Olympisch stadion", "1500");
  lots[24] = lot_new("CE-P14 Westerpark", "1500");
  lots[25] = lot_new("ZO-P02 Arena terrein", "1500");
  lots[26] = lot_new("ZO-P10 Plaza ArenA", "1500");
  lots[27] = lot_new("CE-P06 Byzantium", "1500");
  lots[28] = lot_new("CE-P Willemspoort", "1500");
  lots[29] = lot_new("CE-P01 Sloterdijk", "1500");
  lots[30] = lot_new("CE-P13 Artis", "1500");
  lots[31] = lot_new("CE-P Oosterdok", "1500");
  lots[32] = lot_new("ZO-P06 Pathe/HMH", "1500");
  lots[33] = lot_new("CE-P10 Stadhuis Muziektheater", "1500");
  lots[34] = lot_new("CE-P05 Euro Parking", "1500");
  lots[35] = lot_new("ZO-P03 Mikado", "1500");
  populated = true;
}

static void destroy_lots() {
  for(int i = 0;i < NUM_MENU_ITEMS;i++) {
    lot_destroy(lots[i]);
  }
  free(lots);
}