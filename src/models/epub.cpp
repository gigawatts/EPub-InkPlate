// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.

#define __EPUB__ 1
#include "models/epub.hpp"

#include "models/fonts.hpp"
#include "models/books_dir.hpp"
#include "models/config.hpp"
#include "models/page_locs.hpp"
#include "models/image_factory.hpp"
#include "viewers/msg_viewer.hpp"
#include "viewers/book_viewer.hpp"
#include "helpers/unzip.hpp"

#include "logging.hpp"
#if EPUB_INKPLATE_BUILD
  #include "esp_heap_caps.h"
#endif

#include <iostream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <cctype>

using namespace pugi;

const char * TAG = "EPUB";

bool 
package_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "package") == 0) || (strcmp(node.name(), "opf:package") == 0);
  LOG_D("package() result: %d", res);
  return res;
} 

bool 
metadata_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "metadata") == 0) || (strcmp(node.name(), "opf:metadata") == 0);
  LOG_D("metadata() result: %d", res);
  return res;
} 

bool 
manifest_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "manifest") == 0) || (strcmp(node.name(), "opf:manifest") == 0);
  LOG_D("manifest() result: %d", res);
  return res;
} 

bool 
item_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "item") == 0) || (strcmp(node.name(), "opf:item") == 0);
  LOG_D("item() result: %d", res);
  return res;
} 

bool 
spine_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "spine") == 0) || (strcmp(node.name(), "opf:spine") == 0);
  LOG_D("spine() result: %d", res);
  return res;
} 

bool 
itemref_pred(xml_node node)
{
  bool res = (strcmp(node.name(), "itemref") == 0) || (strcmp(node.name(), "opf:itemref") == 0);
  LOG_D("itemref() result: %d", res);
  return res;
}

bool
xmlns_pred(xml_attribute attr)
{
  bool res = (strcmp(attr.name(), "xmlns") == 0) || (strcmp(attr.name(), "xmlns:opf") == 0);
  LOG_D("xmlns() result: %d", res);
  return res;
}

xml_node one_by_attr(xml_node n, const char * name1, const char * name2, const char * attr, const char * value)
{
  xml_node res;
  
  if (!(res = n.find_child_by_attribute(name1, attr, value))) {
    if ((res = n.find_child_by_attribute(name2, attr, value))) {
      LOG_D("one by attr: %s Found", name2);
    }
    else LOG_D("one by attr: %s NOT Found", name2);
  }
  else LOG_D("one by attr: %s Found", name1);

  return res;
}

EPub::EPub()
{
  opf_data               = nullptr;
  encryption_data        = nullptr;
  current_item_info.data = nullptr;
  file_is_open           = false;
  fonts_size_too_large   = false;
  fonts_size             = 0;
  current_itemref        = xml_node(NULL);
  opf_base_path.clear();
  current_filename.clear();
}

EPub::~EPub()
{
  close_file();
}

void
extract_path(const char * fname, std::string & path)
{
  path.clear();
  int i = strlen(fname) - 1;
  while ((i > 0) && (fname[i] != '/')) i--;
  if (i > 0) path.assign(fname, ++i);
}

bool
EPub::check_mimetype()
{
  char   * data;
  uint32_t size;

  // A file named 'mimetype' must be present and must contain the
  // string 'application/epub+zip'

  LOG_D("Check mimetype.");
  if (!(data = unzip.get_file("mimetype", size))) return false;
  if (strncmp(data, "application/epub+zip", 20)) {
    LOG_E("This is not an EPUB ebook format.");
    free(data);
    return false;
  }

  free(data);
  return true;
}

#define ERR(e) { err = e; break; }

EPub::ObfuscationType
EPub::get_file_obfuscation(const char * filename)
{
  ObfuscationType obf_type = ObfuscationType::NONE;

  if (encryption_is_present()) {
    for (auto & n : encryption.child("encryption").children("enc:EncryptedData")) {
      if (strcmp(n.child("enc:CipherData")
                  .child("enc:CipherReference")
                  .attribute("URI")
                  .value(), filename) == 0) {
        xml_attribute attr = n.child("enc:EncryptionMethod").attribute("Algorithm");
        if (strcmp(attr.value(), "http://ns.adobe.com/pdf/enc#RC") == 0) {
          obf_type = ObfuscationType::ADOBE;
        } 
        else if (strcmp(attr.value(), "http://www.idpf.org/2008/embedding") == 0) {
          obf_type = ObfuscationType::IDPF;
        } 
        else obf_type = ObfuscationType::UNKNOWN;
        break;
      }
    }
  }
  
  return obf_type;
}

bool
EPub::get_encryption_xml()
{
  static constexpr const char * fname = "META-INF/encryption.xml";

  uint32_t size;

  encryption_present = false;

  if ((unzip.file_exists(fname) && 
      (encryption_data = unzip.get_file(fname, size)) != nullptr)) {
    xml_parse_result res = encryption.load_buffer_inplace(encryption_data, size);
    if (res.status != status_ok) {
      LOG_E("encryption.xml load error: %d", res.status);
      free(encryption_data);
      encryption_data = nullptr;
      return false;
    }

    if ((strcmp(encryption.child("encryption")
                          .attribute("xmlns")
                          .value(), 
                "urn:oasis:names:tc:opendocument:xmlns:container") != 0) ||
        (strcmp(encryption.child("encryption")
                          .attribute("xmlns:enc")
                          .value(), 
                "http://www.w3.org/2001/04/xmlenc#") != 0)) {

      LOG_E("encryption.xml file format not supported.");

      encryption.reset();
      free(encryption_data);
      encryption_data    = nullptr;

      return false;
    }
    get_keys();
    encryption_present = true;
  }

  return true;
}

bool
EPub::get_opf_filename(std::string & filename)
{
  int          err = 0;
  char       * data;
  uint32_t     size;

  // A file named 'META-INF/container.xml' must be present and point to the OPF file
  LOG_D("Check container.xml.");
  if (!(data = unzip.get_file("META-INF/container.xml", size))) return false;
  
  xml_document    doc;
  xml_node        node;
  xml_attribute   attr;

  xml_parse_result res = doc.load_buffer_inplace(data, size);
  if (res.status != status_ok) {
    LOG_E("xml load error: %d", res.status);
    free(data);
    return false;
  }

  bool completed = false;
  while (!completed) {
    if (!(node = doc.child("container"))) ERR(1);
    if (!((attr = node.attribute("version")) && (strcmp(attr.value(), "1.0") == 0))) ERR(2);
    if (!(attr = node.child("rootfiles")
                     .find_child_by_attribute("rootfile", "media-type", "application/oebps-package+xml")
                     .attribute("full-path"))) ERR(3);

    filename.assign(attr.value());
    completed = true;
  }

  if (!completed) {
    LOG_E("EPub get_opf error: %d", err);
  }

  doc.reset(); 
  free(data);

  return completed;
}

std::string
EPub::get_unique_identifier()
{
  xml_attribute attr;
  xml_node      node, node2;
  const char  * id;

  if ((node  = opf.find_child(package_pred)) &&
      (id    = node.attribute("unique-identifier").value()) &&
      (node2 = node.find_child(metadata_pred)) &&
      (node2.find_child_by_attribute("dc:identifier", "id", id))) {
    return node2.text().get();
  }
  return "";
}

uint8_t 
hex_to_bin(char ch)
{
  if ((ch >= '0') && (ch <= '9')) return ch - '0';
  if ((ch >= 'A') && (ch <= 'F')) return ch - 'A' + 10;
  if ((ch >= 'a') && (ch <= 'f')) return ch - 'a' + 10;
  return 0;
}

inline bool 
valid_hex(char ch) 
{
  return (((ch >= '0') && (ch <= '9')) ||
          ((ch >= 'A') && (ch <= 'F')) ||
          ((ch >= 'a') && (ch <= 'f')));
}

inline bool
to_bin(const char * from, uint8_t * to) 
{
  if (valid_hex(from[0]) && valid_hex(from[1])) {
    *to = (hex_to_bin(from[0]) << 4) + hex_to_bin(from[1]);
    return true;
  }
  else return false;
}

#if EPUB_INKPLATE_BUILD
  #include "mbedtls/md.h"

  void
  EPub::sha1(const std::string & data) 
  {
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA1;
  
    mbedtls_md_init  (&ctx);
    mbedtls_md_setup (&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (unsigned char *)data.c_str(), data.length());
    mbedtls_md_finish(&ctx, (unsigned char *)&sha_uuid);
    mbedtls_md_free  (&ctx);
  }
#else
  #include <openssl/sha.h>

  void
  EPub::sha1(const std::string & data)
  {
    SHA1((unsigned char *)data.c_str(), data.length(), (uint8_t *)&sha_uuid);
  }
#endif

bool
EPub::get_keys()
{
  std::string  unique_id = get_unique_identifier();
  const char * str       = unique_id.c_str();

  if (unique_id.empty()) return false;
  uint8_t pos = (unique_id.substr(0, 9) == "urn:uuid:") ? 9 : 0;

  // Basic validity checks
  if ((unique_id.length() == (pos + 36)) &&
      (str[pos +  8] == '-') &&
      (str[pos + 13] == '-') &&
      (str[pos + 18] == '-') &&
      (str[pos + 23] == '-')) {

    // Convert it to binary big-endien version
    static const uint8_t idxs[16] = { 0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34 };
    for (uint8_t idx = 0; idx < 16; idx++) {
      if (!to_bin(&str[pos + idxs[idx]], &bin_uuid[idx])) return false;
    }
  }

  unique_id.erase(std::remove_if(unique_id.begin(), unique_id.end(), ::isspace), unique_id.end()); 
  sha1(unique_id);

  return true;
}

bool 
EPub::get_opf(std::string & filename)
{
  int err = 0;
  uint32_t size;

  xml_node      node;
  xml_attribute attr;

  bool completed = false;
  while (!completed) {
    extract_path(filename.c_str(), opf_base_path);
    LOG_D("opf_base_path: %s", opf_base_path.c_str());

    if (!(opf_data = unzip.get_file(filename.c_str(), size))) ERR(6);

    xml_parse_result res = opf.load_buffer_inplace(opf_data, size);
    if (res.status != status_ok) {
      LOG_E("xml load error: %d", res.status);
      opf.reset();
      free(opf_data);
      opf_data = nullptr;
      return false;
    }

    // Verifie that the OPF is of one of the version understood by this application
    if (!((node = opf.find_child(package_pred)) && 
          (attr = node.find_attribute(xmlns_pred)) &&
          (strcmp(attr.value(), "http://www.idpf.org/2007/opf") == 0) &&
          (attr = node.attribute("version")) &&
          ((strcmp(attr.value(), "1.0") == 0) || 
           (strcmp(attr.value(), "2.0") == 0) || 
           (strcmp(attr.value(), "3.0") == 0)))) {
      LOG_E("This book is not compatible with this software.");
      break;
    }

    completed = true;
  }

  if (!completed) {
    LOG_E("EPub get_opf error: %d", err);
    opf.reset();
    free(opf_data);
  }
 
  LOG_D("get_opf() completed.");

  return completed;
}

std::string 
EPub::filename_locate(const char * fname)
{
  char name[256];
  uint8_t idx = 0;
  const char * s = fname;
  while ((idx < 255) && (*s != 0)) {
    if (*s == '%') {
      name[idx++] = (hex_to_bin(s[1]) << 4) + hex_to_bin(s[2]);
      s += 3;
    }
    else if ((*s == '/') && (s[1] == '.') && (s[2] == '.') && (s[3] == '/')) {
      while (idx > 0) {
        idx -= 1;
        if (name[idx] == '/') break;
      }
      s += idx > 0 ? 3 : 4;
    }
    else {
      name[idx++] = *s++;
    }
  }
  name[idx] = 0;

  std::string filename = opf_base_path;
  filename.append(name);

  return filename;
}

char *
EPub::retrieve_file(const char * fname, uint32_t & size)
{
  // Cleanup the filename that can contain characters as hexadecimal values
  // stating with '%' and relative folder change using '../'

  LOG_D("Retrieving file %s", fname);
  
  std::string filename = filename_locate(fname);

  // LOG_D("Retrieving file %s", filename.c_str());

  char * str = unzip.get_file(filename.c_str(), size);

  return str;
}

void
EPub::load_fonts()
{
  for (auto * css: css_cache) {
    retrieve_fonts_from_css(*css);
  }
}

void
EPub::decrypt(void * buffer, const uint32_t size, ObfuscationType obf_type)
{
  uint16_t decrypt_length;
  uint8_t * key;
  uint8_t   key_size;

  if (obf_type == ObfuscationType::ADOBE) {
    decrypt_length = 1024;
    key_size       = 16;
    key            = (uint8_t *) &bin_uuid;
  }
  else if (obf_type == ObfuscationType::IDPF) {
    decrypt_length = 1040;
    key_size       = 20;
    key            = (uint8_t *) &sha_uuid;
  }
  else return;

  uint16_t  length = (size > decrypt_length) ? decrypt_length : size;
  uint8_t  key_idx = 0;
  uint8_t    * str = (uint8_t *) buffer;

  while (length--) {
    *str++ ^= key[key_idx++];
    if (key_idx >= key_size) key_idx = 0;
  }
}

bool
EPub::load_font(const std::string      filename, 
                const std::string      font_family, 
                const Fonts::FaceStyle style)
{
  uint32_t size;
  LOG_D("Font file name: %s", filename.c_str());
  if ((size = unzip.get_file_size(filename.c_str())) > 0) {
    if ((fonts_size + size) > 800000) {
      fonts_size_too_large = true;
      LOG_E("Fonts are using too much space (max 800K). Kept the first fonts read.");
    }
    else {
      unsigned char * buffer;
      ObfuscationType obf_type = get_file_obfuscation(filename.c_str());
      if (obf_type != ObfuscationType::NONE) {
        if (obf_type != ObfuscationType::UNKNOWN) {
          buffer = (unsigned char *) unzip.get_file(filename.c_str(), size);
          if (buffer == nullptr) {
            LOG_E("Unable to retrieve font file: %s", filename.c_str());
          }
          else {
            decrypt(buffer, size, obf_type);

            if (fonts.add(font_family, style, buffer, size, filename)) {
              fonts_size += size;
              return true;
            }
          }   
        }
        else {
          LOG_E("Font %s obfuscated with an unknown algorithm.", filename.c_str());
        }
      }
      else {
        buffer = (unsigned char *) unzip.get_file(filename.c_str(), size);
        if (buffer == nullptr) {
          LOG_E("Unable to retrieve font file: %s", filename.c_str());
        }
        else {
          if (fonts.add(font_family, style, buffer, size, filename)) {
            fonts_size += size;
            return true;
          }
        }
      }
    }
  }

  return false;
}

void
EPub::retrieve_fonts_from_css(CSS & css)
{
  LOG_D("retrieve_fonts_from_css()");
  #if EPUB_INKPLATE_BUILD && (LOG_LOCAL_LEVEL == ESP_LOG_VERBOSE)
    ESP::show_heaps_info();
  #endif
  #if USE_EPUB_FONTS

    if ((book_format_params.use_fonts_in_book == 0) ||
        (fonts_size_too_large)) return;
    
    CSS::RulesMap font_rules;
    DOM * dom = new DOM;
    DOM::Node * ff = dom->body->add_child(DOM::Tag::FONT_FACE);

    css.match(ff, font_rules);

    delete dom;
    dom = nullptr;
    
    if (font_rules.empty()) return;

    bool first = true;

    for (auto & rule : font_rules) {
      const CSS::Values * values;
      if ((values = css.get_values_from_props(*rule.second, CSS::PropertyId::FONT_FAMILY))) {

        Fonts::FaceStyle style       = Fonts::FaceStyle::NORMAL;
        Fonts::FaceStyle font_weight = Fonts::FaceStyle::NORMAL;
        Fonts::FaceStyle font_style  = Fonts::FaceStyle::NORMAL;
        std::string      font_family = values->front()->str;

        if ((values = css.get_values_from_props(*rule.second, CSS::PropertyId::FONT_STYLE))) {
          font_style = (Fonts::FaceStyle) values->front()->choice.face_style;
        }
        if ((values = css.get_values_from_props(*rule.second, CSS::PropertyId::FONT_WEIGHT))) {
          font_weight = (Fonts::FaceStyle) values->front()->choice.face_style;
        }
        style = fonts.adjust_font_style(style, font_style, font_weight);
        // LOG_D("Style: %d text-style: %d text-weight: %d", style, font_style, font_weight);

        if (fonts.get_index(font_family.c_str(), style) == -1) { // If not already loaded
          if ((values = css.get_values_from_props(*rule.second, CSS::PropertyId::SRC)) &&
              (!values->empty()) &&
              (values->front()->value_type == CSS::ValueType::URL)) {

            if (first) {
              first = false;
              LOG_D("Displaying font loading msg.");
              msg_viewer.show(
                MsgViewer::MsgType::INFO, 
                false, false, 
                "Retrieving Font(s)", 
                "The application is retrieving font(s) from the EPub file. Please wait."
              );
            }

            std::string filename = css.get_folder_path() + values->front()->str;
            filename = filename_locate(filename.c_str());

            load_font(filename, font_family, style);
            if (fonts_size_too_large) break;
          }
        }
      }
    }
  #endif
  LOG_D("end of retrieve_fonts_from_css()");
  #if EPUB_INKPLATE_BUILD && (LOG_LOCAL_LEVEL == ESP_LOG_VERBOSE)
    ESP::show_heaps_info();
  #endif
}

void
EPub::retrieve_css(ItemInfo & item)
{

  // Retrieve css files, puting them in the css_cache vector (as a cache).
  // The properties are then merged into the current_css map for the item
  // being processed.

  LOG_D("retrieve_css()");
  #if EPUB_INKPLATE_BUILD && (LOG_LOCAL_LEVEL == ESP_LOG_VERBOSE)
    ESP::show_heaps_info();
  #endif

  xml_node      node;
  xml_attribute attr;
  
  if ((node = item.xml_doc.child("html").child("head").child("link"))) {
    do {
      if ((attr = node.attribute("type")) &&
          (strcmp(attr.value(), "text/css") == 0) &&
          (attr = node.attribute("href"))) {

        std::string css_id = attr.value(); // uses href as id

        // search the list of css files to see if it already been parsed
        int16_t idx = 0;
        CSSList::iterator css_cache_it = css_cache.begin();

        while (css_cache_it != css_cache.end()) {
          if ((*css_cache_it)->get_id().compare(css_id) == 0) break;
          css_cache_it++;
          idx++;
        }
        if (css_cache_it == css_cache.end()) {

          // The css file was not found. Load it in the cache.
          uint32_t size;
          std::string fname = item.file_path;
          fname.append(css_id.c_str());
          char * data = retrieve_file(fname.c_str(), size);

          if (data != nullptr) {
            #if COMPUTE_SIZE
              memory_used += size;
            #endif
            LOG_D("CSS Filename: %s", fname.c_str());
            std::string path;
            extract_path(fname.c_str(), path);
            CSS * css_tmp = new CSS(css_id.c_str(), path.c_str(), data, size, 0);
            if (css_tmp == nullptr) msg_viewer.out_of_memory("css temp allocation");
            free(data);

            // #if DEBUGGING
            //   css_tmp->show();
            // #endif

            retrieve_fonts_from_css(*css_tmp);
                css_cache.push_back(css_tmp);
            item.css_list.push_back(css_tmp);
          }
        } 
        else {
          item.css_list.push_back(*css_cache_it);
        }
      }
    } while ((node = node.next_sibling("link")));
  }

  // Now look at <style> tags presents in the <html><head>, creating a temporary
  // css object for each of them.

  if ((node = item.xml_doc.child("html").child("head" ).child("style"))) {
    do {
      xml_node sub = node.first_child();
      const char * buffer;
      if (sub != nullptr) {
        buffer = sub.value();
      }
      else {
        buffer = node.child_value();
      }
      CSS * css_tmp = new CSS("current-item", item.file_path.c_str(), buffer, strlen(buffer), 1);
      if (css_tmp == nullptr) msg_viewer.out_of_memory("css temp allocation");
      retrieve_fonts_from_css(*css_tmp);
      // css_tmp->show();
      item.css_cache.push_back(css_tmp);
    } while ((node = node.next_sibling("style")));
  }

  // Populate the current item css structure with property suites present in
  // the identified css files in the <meta> portion of the html file.

  if (item.css != nullptr) delete item.css;
  if ((item.css = new CSS("MergedForItem")) == nullptr) {
    msg_viewer.out_of_memory("css allocation");
  }
  for (auto * css : item.css_list ) item.css->retrieve_data_from_css(*css);
  for (auto * css : item.css_cache) item.css->retrieve_data_from_css(*css);

  // item.css->show();
  LOG_D("end of retrieve_css()");
  #if EPUB_INKPLATE_BUILD && (LOG_LOCAL_LEVEL == ESP_LOG_VERBOSE)
    ESP::show_heaps_info();
  #endif
}


bool 
EPub::get_item(pugi::xml_node itemref, 
               ItemInfo &     item)
{
  int err = 0;
  #define ERR(e) { err = e; break; }

  if (!file_is_open) return false;

  // if ((item.data != nullptr) && (current_itemref == itemref))
  // return true;

  clear_item_data(item);

  xml_node      node, node2;
  xml_attribute attr;

  const char * id = itemref.attribute("idref").value();

  bool completed = false;

  while (!completed) {
    if (!((node = opf.find_child(package_pred).find_child(manifest_pred)) &&
          (node = one_by_attr(node, "item", "opf:item", "id", id)))) ERR(1);

    if (!(attr = node.attribute("media-type"))) ERR(2);
    const char* media_type = attr.value();

    if      (strcmp(media_type, "application/xhtml+xml") == 0) item.media_type = MediaType::XML;
    else if (strcmp(media_type, "image/jpeg"           ) == 0) item.media_type = MediaType::JPEG;
    else if (strcmp(media_type, "image/png"            ) == 0) item.media_type = MediaType::PNG;
    else if (strcmp(media_type, "image/bmp"            ) == 0) item.media_type = MediaType::BMP;
    else if (strcmp(media_type, "image/gif"            ) == 0) item.media_type = MediaType::GIF;
    else ERR(3);

    if (!(attr = node.attribute("href"))) ERR(5);

    LOG_D("Retrieving file %s", attr.value());

    uint32_t size;
    extract_path(attr.value(), item.file_path);

    // LOG_D("item.file_path: %s.", item.file_path.c_str());

    if ((item.data = retrieve_file(attr.value(), size)) == nullptr) ERR(6);

    if (item.media_type == MediaType::XML) {

      char * str;
      while ((str = strstr(item.data, "/*<![CDATA[*/")) != nullptr) {
        *str++ = ' ';
        *str   = ' ';
        str   +=  10;
        *str++ = ' ';
        *str   = ' ';
      }
      while ((str = strstr(item.data, "/*]]>*/")) != nullptr) {
        *str++ = ' ';
        *str   = ' ';
        str   +=   4;
        *str++ = ' ';
        *str   = ' ';
      }
      LOG_D("Reading file %s", attr.value());

      xml_parse_result res = item.xml_doc.load_buffer_inplace(item.data, size);
      if (res.status != status_ok) {
        LOG_E("item_doc xml load error: %d", res.status);
        // msg_viewer.show(
        //   MsgViewer::MsgType::ALERT, 
        //   true, false, 
        //   "XML Error in eBook.", 
        //   "File %s contains XHTML errors and cannot be loaded.",
        //   attr.value()
        // );
        item.xml_doc.reset();
        if (item.data != nullptr) {
          free(item.data);
          item.data = nullptr;
        }
        return false;
      }

      // current_item.parse<0>(current_item_data);

      // if (css.size() > 0) css.clear();

     retrieve_css(item);
    }

    completed = true;
  }

  if (!completed) {
    LOG_E("EPub get_current_item error: %d", err);
    clear_item_data(item);
  }
  return completed;
}

void 
EPub::update_book_format_params()
{
  constexpr int8_t default_value = -1;

  if (book_params == nullptr) {
    book_format_params = {
      .ident             = Screen::IDENT,
      .orientation       =  0,  // Get de compiler happy (no warning). Will be set below...
      .show_title        =  0,  // ... idem ...
      .show_images       = default_value,
      .font_size         = default_value,
      .use_fonts_in_book = default_value,
      .font              = default_value
    };
  }
  else {
    book_params->get(BookParams::Ident::SHOW_IMAGES,        &book_format_params.show_images      );
    book_params->get(BookParams::Ident::FONT_SIZE,          &book_format_params.font_size        );
    book_params->get(BookParams::Ident::USE_FONTS_IN_BOOK,  &book_format_params.use_fonts_in_book);
    book_params->get(BookParams::Ident::FONT,               &book_format_params.font             );
  }

  config.get(Config::Ident::ORIENTATION, &book_format_params.orientation);
  config.get(Config::Ident::SHOW_TITLE,  &book_format_params.show_title );

  if (book_format_params.show_images       == default_value) config.get(Config::Ident::SHOW_IMAGES,        &book_format_params.show_images      );
  if (book_format_params.font_size         == default_value) config.get(Config::Ident::FONT_SIZE,          &book_format_params.font_size        );
  if (book_format_params.use_fonts_in_book == default_value) config.get(Config::Ident::USE_FONTS_IN_BOOKS, &book_format_params.use_fonts_in_book);
  if (book_format_params.font              == default_value) config.get(Config::Ident::DEFAULT_FONT,       &book_format_params.font             );

  //if (!book_format_params.use_fonts_in_book) fonts.clear();
}

void
EPub::open_params(const std::string & epub_filename)
{
  std::string params_filename = epub_filename.substr(0, epub_filename.find_last_of('.')) + ".pars";
  book_params = new BookParams(params_filename, false);
  if (book_params != nullptr) {
    book_params->read();
  }
}

bool 
EPub::open_file(const std::string & epub_filename)
{
  if (file_is_open && (current_filename == epub_filename)) return true;
  if (file_is_open) close_file();

  page_locs.clear();
  
  #if COMPUTE_SIZE
    memory_used = 0;
  #endif

  LOG_D("Opening EPub file through unzip...");
  if (!unzip.open_zip_file(epub_filename.c_str())) {
    LOG_E("EPub open_file: Unable to open zip file: %s", epub_filename.c_str());
    return false;
  }

  if (!check_mimetype()) return false;

  LOG_D("Getting the OPF file");
  std::string filename;
  if (!get_opf_filename(filename)) return false;

  if (!get_opf(filename)) {
    LOG_E("EPub open_file: Unable to get opf of %s", epub_filename.c_str());
    unzip.close_zip_file();
    return false;
  }

  get_encryption_xml();

  open_params(epub_filename);
  update_book_format_params();

  fonts.adjust_default_font(book_format_params.font);

  clear_item_data(current_item_info);

  current_filename     = epub_filename;
  file_is_open         = true;
  fonts_size_too_large = false;
  fonts_size           = 0;

  LOG_D("EPub file is now open.");

  return true;
}

void
EPub::clear_item_data(ItemInfo & item)
{
  item.xml_doc.reset();
  if (item.data != nullptr) {
    free(item.data);
    item.data = nullptr;
  }

  // for (auto * css : current_item_css_list) {
  //   delete css;
  // }
  item.css_list.clear();

  for (auto * css : item.css_cache) delete css;
  item.css_cache.clear();

  item.itemref_index = -1;
}

bool 
EPub::close_file()
{
  if (!file_is_open) return true;

  clear_item_data(current_item_info);

  if (opf_data) {
    opf.reset();
    free(opf_data);
    opf_data = nullptr;
  }

  opf_base_path.clear();

  if (encryption_data) {
    encryption.reset();
    free(encryption_data);
    encryption_data = nullptr;
  }

  unzip.close_zip_file();

  for (auto * css : css_cache) delete css;

  css_cache.clear();
  fonts.clear();

  file_is_open = false;
  encryption_present = false;
  current_filename.clear();

  if (book_params != nullptr) {
    book_params->save();
    delete book_params;
    book_params = nullptr;
  }

  return true;
}

const char * 
EPub::get_meta(const std::string & name)
{
  if (!file_is_open) return nullptr;

  xml_node node;
  
  if ((node = opf.find_child(package_pred).find_child(metadata_pred))) {
    return node.child_value(name.c_str());
  }
  return nullptr;

  // if (!((node = opf.child("package" ).child("metadata")))) {
  //   node = opf.child("package").child("opf:metadata");
  // }
  // return node == nullptr ? nullptr : node.child_value(name.c_str());
}

const char *
EPub::get_cover_filename()
{
  if (!file_is_open) return nullptr;

  xml_node      node;
  xml_attribute attr;

  const char * itemref = nullptr;
  const char * filename = nullptr;

  // First, try to find its from metadata

  if ((node = opf.find_child(package_pred)
                 .find_child(metadata_pred)) &&
      (node = one_by_attr(node, "meta", "opf:meta", "name", "cover")) &&
      (itemref = node.attribute("content").value())) {

    for (auto n : opf.find_child(package_pred).find_child(manifest_pred).children()) {
      if ((strcmp(n.name(), "item") == 0) || (strcmp(n.name(), "opf:item") == 0)) {
        if ((((attr = n.attribute("id"        )) && (strcmp(attr.value(), itemref) == 0)) ||
            ((attr = n.attribute("properties")) && (strcmp(attr.value(), itemref) == 0))) &&
            (attr = n.attribute("href"))) {
          filename = attr.value();
          break;
        }
      }
    }
  }

  if (filename == nullptr) {
    // Look inside manifest
    for (auto n : opf.find_child(package_pred).find_child(manifest_pred).children()) {
      if ((strcmp(n.name(), "item") == 0) || (strcmp(n.name(), "opf:item") == 0)) {
        if ((attr = n.attribute("id")) && 
            ((strcmp(attr.value(), "cover-image") == 0) || 
             (strcmp(attr.value(), "cover"      ) == 0)) && 
            (attr = n.attribute("href"))) {
          filename = attr.value();
          break;
        }
      }
    }
  }

  return filename == nullptr ? "" : filename;
}

int16_t 
EPub::get_item_count()
{
  if (!file_is_open) return 0;

  auto it = opf.find_child(package_pred).find_child(spine_pred).children("itemref");
  int16_t count = std::distance(it.begin(), it.end());
  
  if (count == 0) {
    it = opf.find_child(package_pred).find_child(spine_pred).children("opf:itemref");
    count = std::distance(it.begin(), it.end());
  }

  LOG_D("Item count: %d", count);
  return count;
}

bool 
EPub::get_item_at_index(int16_t itemref_index)
{
  if (!file_is_open) return false;

  if (current_item_info.itemref_index == itemref_index) return true;
  
  xml_node node  = xml_node();
  int16_t  index = 0;

  for (auto n : opf.find_child(package_pred).find_child(spine_pred).children()) {
    if (index == itemref_index) { node = n; break; }
    index++;
  }

  if (node == nullptr) return false;

  bool res = false;

  if ((current_item_info.data == nullptr) || (current_itemref != node)) {
    if ((res = get_item(node, current_item_info))) current_itemref = node;
    current_item_info.itemref_index = itemref_index;
  }
  return res;
}

// This is in support of the pages location retrieval mechanism. The ItemInfo
// is being used to retrieve asynchroniously the book page numbers without
// interfering with the main book viewer thread.
bool 
EPub::get_item_at_index(int16_t    itemref_index, 
                        ItemInfo & item)
{
  if (!file_is_open) return false;

  LOG_D("Mutex lock...");
  
  { std::scoped_lock guard(mutex);
    
    xml_node node = xml_node();
    int16_t index = 0;

    for (auto n : opf.find_child(package_pred).find_child(spine_pred).children()) {
      if (index == itemref_index) { node = n; break; }
      index++;
    }

    bool res = false;

    if (node) {
      res = get_item(node, item);
      item.itemref_index = itemref_index;
    }

    LOG_D("Mutex unlocked...");
    return res;
  }
}

Image *
EPub::get_image(std::string & fname, bool load)
{
  LOG_D("Mutex lock...");

  { std::scoped_lock guard(mutex);

    std::string filename = filename_locate(fname.c_str());
    Image * img = ImageFactory::create(filename, 
                                       Dim(Screen::get_width(), Screen::get_height()), 
                                       load);

    if ((img == nullptr) || 
        (load && (img->get_bitmap() == nullptr)) ||
        (img->get_dim().height == 0) ||
        (img->get_dim().width  == 0)) {
      if (img != nullptr) delete img;
      img = nullptr;
    }

    // if (img->get_bitmap() != nullptr) {
    //   std::cout << "----- Image content -----" << std::endl;
    //   for (int i = 0; i < 200; i++) {
    //     std::cout << std::hex << std::setw(2) << +img->get_bitmap()[i];
    //   }
    //   std::cout << std::endl << "-----" << std::endl;
    // }
    LOG_D("Mutex unlocked...");
    return img;
  }
}