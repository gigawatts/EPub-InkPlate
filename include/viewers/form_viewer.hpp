// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#pragma once
#include "global.hpp"

#include "controllers/event_mgr.hpp"
#include "models/fonts.hpp"
#include "viewers/page.hpp"
#include "memory_pool.hpp"

#include <list>

enum class FormEntryType { HORIZONTAL, VERTICAL, UINT16
  #if INKPLATE_6PLUS || TOUCH_TRIAL
    , DONE
  #endif
};

constexpr  uint8_t FORM_FONT_SIZE = 9;

struct Choice {
  const char   * caption;
  int8_t         value;
};

struct FormEntry {
  const char   * caption;
  void         * value;
  int8_t         choice_count;
  const Choice * choices;
  FormEntryType  entry_type;
};

class FormField
{
  public:
    FormField(FormEntry & form_entry, Font & font) :
      form_entry(form_entry), font(font) { 
    };
    virtual ~FormField() { };

    inline const Dim &   get_field_dim() { return field_dim;   }
    inline const Dim & get_caption_dim() { return caption_dim; }

    inline const Pos &   get_field_pos() { return field_pos;   }
    inline const Pos & get_caption_pos() { return caption_pos; }

    void compute_caption_dim() {
      if (form_entry.caption != nullptr) {
        font.get_size(form_entry.caption, &caption_dim, FORM_FONT_SIZE);
      }
      else {
        caption_dim = Dim(0, 0);
      }
    }

    virtual void             paint(Page::Format & fmt)            = 0;
    virtual void compute_field_dim()                              = 0;
    virtual void compute_field_pos(Pos from_pos)                  = 0;
    virtual void  update_highlight()                              = 0;
    virtual void        save_value()                              = 0;

    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      virtual void  event(const EventMgr::Event & event)          = 0;
    #else
      virtual bool edit(uint16_t x, uint16_t y) { return false; }
    #endif

    
    void compute_caption_pos(Pos from_pos) {
      caption_pos = { (uint16_t)(from_pos.x - caption_dim.width), 
                      (uint16_t)(from_pos.y) };
    }

    void show_highlighted(bool show_it) {
      if (show_it) {
        page.put_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                           Pos(field_pos.x - 10, field_pos.y - 10));
      }
      else {
        page.clear_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                             Pos(field_pos.x - 10, field_pos.y - 10));
      }
    }

    void show_selected(bool show_it) {
      if (show_it) {
        page.put_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                           Pos(field_pos.x - 10, field_pos.y - 10));
        page.put_highlight(Dim(field_dim.width + 22, field_dim.height + 22),
                           Pos(field_pos.x - 11, field_pos.y - 11));
        page.put_highlight(Dim(field_dim.width + 24, field_dim.height + 24),
                           Pos(field_pos.x - 12, field_pos.y - 12));
      }
      else {
        page.clear_highlight(Dim(field_dim.width + 20, field_dim.height + 20),
                             Pos(field_pos.x -10, field_pos.y - 10));
        page.clear_highlight(Dim(field_dim.width + 22, field_dim.height + 22),
                             Pos(field_pos.x -11, field_pos.y - 11));
        page.clear_highlight(Dim(field_dim.width + 24, field_dim.height + 24),
                             Pos(field_pos.x -12, field_pos.y - 12));
      }
    }

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      inline bool is_pointed(uint16_t x, uint16_t y) {
        return (x >= (field_pos.x - 10)) && 
               (x <= (field_pos.x + field_dim.width + 10)) &&
               (y >= (field_pos.y - 10)) &&
               (y <= (field_pos.y + field_dim.height + 10));
      } 

    #endif

  protected:
    
    FormEntry & form_entry;
    Font      & font;
    Dim         field_dim, caption_dim;
    Pos         field_pos, caption_pos;

};

class FormChoice : public FormField
{
  public:
    static constexpr Choice done_choices[1] = {
      { "DONE",       1 }
    };

    static constexpr Choice dir_view_choices[2] = {
      { "LINEAR",     0 },
      { "MATRIX",     1 }
    };

    static constexpr Choice ok_cancel_choices[2] = {
      { "OK",         1 },
      { "CANCEL",     0 }
    };

    static constexpr Choice yes_no_choices[2] = {
      { "YES",        1 },
      { "NO",         0 }
    };

    static constexpr Choice resolution_choices[2] = {
      { "1Bit",       0 },
      { "3Bits",      1 }
    };

    static constexpr Choice timeout_choices[3] = {
      { "5",          5 },
      { "15",        15 },
      { "30",        30 }
    };

    static constexpr Choice battery_visual_choices[4] = {
      { "NONE",       0 },
      { "PERCENT",    1 },
      { "VOLTAGE",    2 },
      { "ICON",       3 }
    };

    static constexpr Choice font_size_choices[4] = {
      { "8",          8 },
      { "10",        10 },
      { "12",        12 },
      { "15",        15 }
    };

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      static constexpr Choice orientation_choices[4] = {
        { "LEFT",     3 },
        { "RIGHT",    2 },
        { "TOP",      1 },
        { "BOTTOM",   0 }
      };
    #else
      static constexpr Choice orientation_choices[3] = {
        { "LEFT",     0 },
        { "RIGHT",    1 },
        { "BOTTOM",   2 }
      };
    #endif

    static Choice  font_choices[8];
    static uint8_t font_choices_count;

    void compute_field_dim() {
      field_dim = { 0, 0 };
      for (int8_t i = 0; i < form_entry.choice_count; i++) {
        Item * item = item_pool.newElement();
        items.push_back(item);
        font.get_size(form_entry.choices[i].caption, &item->dim, FORM_FONT_SIZE);
        item->idx = i;
      }

      int i = 0;
      for (Items::iterator it = items.begin(); it != items.end(); it++) {
        if (form_entry.choices[i].value == * (uint8_t *) form_entry.value) {
          current_item = it;
          break;
        }
        i++;        
      }

      old_item = items.end();
    }

    static void adjust_font_choices(char ** font_names, uint8_t size) {
      for (uint8_t i = 0; i < size; i++) font_choices[i].caption = font_names[i];
      font_choices_count = size; 
    }

    void compute_field_pos(Pos from_pos) = 0;

    void paint(Page::Format & fmt) {

      Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
      uint8_t       offset = -glyph->yoff;
     
      page.put_str_at(form_entry.caption, 
                      { caption_pos.x, (uint16_t)(caption_pos.y + offset) }, 
                      fmt);
      for (auto * item : items) {
        page.put_str_at(form_entry.choices[item->idx].caption, 
                        { item->pos.x, (uint16_t)(item->pos.y + offset) }, 
                        fmt);
      }
    }

    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)

      void event(const EventMgr::Event & event) {
        old_item = current_item;
        switch (event.kind) {
          case EventMgr::EventKind::DBL_PREV:
          case EventMgr::EventKind::PREV:
            if (current_item == items.begin()) current_item = items.end();
            current_item--;
            break;
          case EventMgr::EventKind::DBL_NEXT:
          case EventMgr::EventKind::NEXT:
            current_item++;
            if (current_item == items.end()) current_item = items.begin();
            break;
          default:
            break;
        }
      }
    #else
      bool edit(uint16_t x, uint16_t y) {
        old_item = current_item;
        Items::iterator it;
        for (it = items.begin(); it != items.end(); it++) {
          if ((x >= (*it)->pos.x - 5) && (x <= ((*it)->pos.x + (*it)->dim.width + 5)) &&
              (y >= (*it)->pos.y - 5) && (y <= ((*it)->pos.y + (*it)->dim.height) + 5)) {
            break;
          }
        }
        if (it != items.end()) current_item = it;
        return false;
      }
    #endif

    void update_highlight() {
      if (old_item != current_item) {
        if (old_item != items.end()) {
          page.clear_highlight(Dim((*old_item)->dim.width + 10, (*old_item)->dim.height + 10),
                               Pos((*old_item)->pos.x     -  5, (*old_item)->pos.y      -  5));
        }
        page.put_highlight(Dim((*current_item)->dim.width + 10, (*current_item)->dim.height + 10),
                           Pos((*current_item)->pos.x     -  5, (*current_item)->pos.y      -  5));
      }
      old_item = current_item;
    }

    void save_value() {
      * (int8_t *) form_entry.value = form_entry.choices[(*current_item)->idx].value; 
    }

  protected:
    struct Item {
      Pos     pos;
      Dim     dim;
      uint8_t idx;
    };

    static MemoryPool<Item> item_pool; // Shared by all FormChoice

    typedef std::list<Item *> Items;
    
    Items items;
    Items::iterator current_item, old_item;


  public:
    using FormField::FormField;

   ~FormChoice() {
      for (auto * item : items) {
        item_pool.deleteElement(item);
      }
      items.clear();
    }
};

class VFormChoice : public FormChoice
{
  private:
    static constexpr char const * TAG = "VFormChoice";

  public:
    using FormChoice::FormChoice;

   ~VFormChoice() { }

    void compute_field_pos(Pos from_pos) {
      field_pos   = from_pos; 
      Pos the_pos = from_pos;

      uint8_t line_height = font.get_line_height(FORM_FONT_SIZE);
      for (auto * item : items) {
        item->pos  = the_pos;
        the_pos.y += line_height;
        LOG_D("Item position  [%d, %d]", item->pos.x, item->pos.y);
      } 
    }
    
    void compute_field_dim() {
      FormChoice::compute_field_dim();
      uint8_t line_height = font.get_line_height(FORM_FONT_SIZE);
      uint8_t last_height = 0;
      for (auto * item : items) {
        if (field_dim.width < item->dim.width) field_dim.width = item->dim.width;
        field_dim.height += line_height;
        last_height = item->dim.height;
        LOG_D("Item dimension: [%d, %d]", item->dim.width, item->dim.height);
      }
      field_dim.height += last_height - line_height;
    }
};

class HFormChoice : public FormChoice
{
  private:
    static constexpr char const * TAG = "HFormChoice";

  public:
    using FormChoice::FormChoice;

   ~HFormChoice() { }

    void compute_field_pos(Pos from_pos) { 
      field_pos   = from_pos; 
      Pos the_pos = from_pos;

      for (auto * item : items) {
        item->pos  = the_pos;
        the_pos.x += item->dim.width + 20;
        LOG_D("Item position: [%d, %d]", item->pos.x, item->pos.y);
      }
    }
    
    static constexpr uint8_t HORIZONTAL_SEPARATOR = 20;

    void compute_field_dim() {
      FormChoice::compute_field_dim();
      uint16_t separator = 0;
      for (auto * item : items) {
        if (field_dim.height < item->dim.height) field_dim.height = item->dim.height;
        field_dim.width += item->dim.width + separator;
        separator = HORIZONTAL_SEPARATOR;
        LOG_D("Item dimension: [%d, %d]", item->dim.width, item->dim.height);
      }
    }
};

class FormUInt16 : public FormField
{
  public:
    using FormField::FormField;

    void compute_field_pos(Pos from_pos) { 
      field_pos = from_pos; 
    }

    void paint(Page::Format & fmt) {
      char val[8];
      Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
      uint8_t       offset = -glyph->yoff;

      int_to_str(* (uint16_t *) form_entry.value, val, 8);
      page.put_str_at(form_entry.caption, 
                      { caption_pos.x, (uint16_t)(caption_pos.y + offset) }, 
                      fmt);
      page.put_str_at(val, 
                      { field_pos.x, (uint16_t)(field_pos.y + offset) }, 
                      fmt);
    }

    #if !(INKPLATE_6PLUS || TOUCH_TRIAL)
      void event(const EventMgr::Event & event) { 
      }
    #endif

    void update_highlight() {
    }

    void save_value() {
    }

    void compute_field_dim() {
      font.get_size("XXXXX", &field_dim, FORM_FONT_SIZE);
    }
};

#if INKPLATE_6PLUS || TOUCH_TRIAL
class FormDone : public FormField
{
  public:
    using FormField::FormField;

    bool edit(uint16_t x, uint16_t y) { return true; }

    void save_value() { }
    void update_highlight() { 
      page.put_rounded(
        Dim(field_dim.width  + 16,
            field_dim.height + 16),
        Pos(field_pos.x - 8, field_pos.y - 8));
      page.put_rounded(
        Dim(field_dim.width  + 18,
            field_dim.height + 18),
        Pos(field_pos.x - 9, field_pos.y - 9));
      page.put_rounded(
        Dim(field_dim.width  + 20,
            field_dim.height + 20),
        Pos(field_pos.x - 10, field_pos.y - 10));
    }

    void compute_field_dim() {
      font.get_size(" DONE ", &field_dim, FORM_FONT_SIZE);
    }

    void compute_field_pos(Pos from_pos) {
      field_pos.x = (Screen::WIDTH / 2) - (field_dim.width / 2);
      field_pos.y = from_pos.y;
    }

    void paint(Page::Format & fmt) {
      Font::Glyph * glyph  =  font.get_glyph('M', FORM_FONT_SIZE);
      uint8_t       offset = -glyph->yoff;

      page.put_str_at(" DONE ", 
                      { field_pos.x, (uint16_t)(field_pos.y + offset) }, 
                      fmt);
    }
};
#endif

class FieldFactory
{
  public:
    static FormField * create(FormEntry & entry, Font & font) {
      switch (entry.entry_type) {
        case FormEntryType::HORIZONTAL:
          return new HFormChoice(entry, font);
        case FormEntryType::VERTICAL:
          return new VFormChoice(entry, font);
        case FormEntryType::UINT16:
          return new FormUInt16(entry, font);
      #if INKPLATE_6PLUS || TOUCH_TRIAL
        case FormEntryType::DONE:
          return new FormDone(entry, font);
      #endif
      }
      return nullptr;
    }
};

class FormViewer
{
  private:
    static constexpr char const * TAG = "FormViewer";

    static constexpr uint8_t TOP_YPOS         = 100;
    static constexpr uint8_t BOTTOM_YPOS      =  50;

    uint8_t entry_count;
    int16_t all_fields_width;
    // int16_t last_choices_width;
    int8_t  line_height;
    bool    highlighting_field;
    bool    selecting_field;

    typedef std::list<FormField *> Fields;
    
    Fields fields;
    Fields::iterator current_field;

    #if (INKPLATE_6PLUS || TOUCH_TRIAL)
      Fields::iterator find_field(uint16_t x, uint16_t y) {
        for (Fields::iterator it = fields.begin(); it != fields.end(); it++) {
          if ((*it)->is_pointed(x, y)) return it;
        }

        return fields.end();
      }

    #endif

  public:

    typedef FormEntry * FormEntries;


    void show(FormEntries form_entries, int8_t size, const std::string & bottom_msg) {

      Font * font =  fonts.get(5);

      for (auto * field : fields) delete field;
      fields.clear();

      for (int i = 0; i < size; i++) {
        FormField * field = FieldFactory::create(form_entries[i], *font); 
        if (field != nullptr) {
          fields.push_back(field);
          field->compute_caption_dim();
          field->compute_field_dim();
          LOG_D("Field dimentions: Caption: [%d, %d] Field: [%d, %d]", 
                field->get_caption_dim().width, field->get_caption_dim().height,
                field->get_field_dim().width, field->get_field_dim().height);
        }
      }

      all_fields_width = 0;
      for (auto * field : fields) {
        int16_t width = field->get_field_dim().width;
        if (width > all_fields_width) all_fields_width = width;
      }

      int16_t       current_ypos  = TOP_YPOS + 20;
      const int16_t right_xpos    = Screen::WIDTH - 60;
      int16_t       caption_right = right_xpos - all_fields_width - 35;
      int16_t       field_left    = right_xpos - all_fields_width - 10;

      for (auto * field : fields) {
        field->compute_caption_pos(Pos(caption_right, current_ypos));
        field->compute_field_pos(Pos(field_left, current_ypos));
        current_ypos += field->get_field_dim().height + 20;
        LOG_D("Field positions: Caption: [%d, %d] Field: [%d, %d]", 
              field->get_caption_pos().x, field->get_caption_pos().y,
              field->get_field_pos().x, field->get_field_pos().y);
      }

      Pos bottom_msg_pos = { 40, (uint16_t)(current_ypos + 30) };
      // Display the form

      Page::Format fmt = {
        .line_height_factor =   1.0,
        .font_index         =     5,
        .font_size          = FORM_FONT_SIZE,
        .indent             =     0,
        .margin_left        =     5,
        .margin_right       =     5,
        .margin_top         =     0,
        .margin_bottom      =     0,
        .screen_left        =    20,
        .screen_right       =    20,
        .screen_top         = TOP_YPOS,
        .screen_bottom      = BOTTOM_YPOS,
        .width              =     0,
        .height             =     0,
        .vertical_align     =     0,
        .trim               =  true,
        .pre                = false,
        .font_style         = Fonts::FaceStyle::NORMAL,
        .align              = CSS::Align::LEFT,
        .text_transform     = CSS::TextTransform::NONE,
        .display            = CSS::Display::INLINE
      };

      page.start(fmt);

      // The large rectangle into which the form will be drawn

      page.clear_region(
        Dim(Screen::WIDTH - 40, Screen::HEIGHT - fmt.screen_bottom - fmt.screen_top),
        Pos(20, TOP_YPOS));

      page.put_highlight(
        Dim(Screen::WIDTH - 44, Screen::HEIGHT - fmt.screen_bottom - fmt.screen_top - 4),
        Pos(22, TOP_YPOS + 2));

      // Show all captions (but the last one (OK / CANCEL) or (DONE)) and choices 

      for (auto * field : fields) {
        field->paint(fmt);
        field->update_highlight();
      }

      page.put_str_at(bottom_msg, bottom_msg_pos, fmt);

      current_field = fields.begin();

      selecting_field = false;

      #if (INKPLATE_6PLUS || TOUCH_TRIAL)
        highlighting_field = false;
      #else
        highlighting_field = true;
        (*current_field)->show_highlighted(true);
      #endif

      page.paint(false);
    }

    bool event(const EventMgr::Event & event);
};

#if __FORM_VIEWER__
  FormViewer form_viewer;
#else
  extern FormViewer form_viewer;
#endif
