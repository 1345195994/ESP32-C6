#pragma once
// One draw object, no per-cell widget allocations. Repeats the header on every page.
static void drawScheduleTable(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
  lv_area_t box; lv_obj_get_coords(obj, &box);
  const lv_font_t *font = uploadedFont(0, bodyNativeFont(cfg.scheduleTextSize));
  const lv_font_t *headerFont = bodyNativeFont(16);
  const int width = lv_area_get_width(&box), pitch = font->line_height;
  // Reserve enough space for HH:MM even with a large requested font.
  lv_point_t timeSize;
  lv_txt_get_size(&timeSize, "00:00", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int widths[3] = {min((int)timeSize.x + 8, width / 2), 0, max(40, width / 4)};
  widths[1] = width - widths[0] - widths[2];
  lv_draw_rect_dsc_t border; lv_draw_rect_dsc_init(&border);
  border.bg_opa = LV_OPA_TRANSP; border.border_width = 1;
  border.border_color = lv_color_hex(cfg.color[BODY]); border.radius = 0;
  lv_draw_label_dsc_t label; lv_draw_label_dsc_init(&label);
  label.color = lv_color_hex(cfg.color[BODY]); label.align = LV_TEXT_ALIGN_CENTER;
  const char *headers[] = {"时间", "课程名称", "教室"};
  int y = box.y1, headerHeight = headerFont->line_height + 6;
  for(int c=0;c<3;++c) {
    lv_point_t size; lv_txt_get_size(&size,headers[c],headerFont,0,0,widths[c]-6,LV_TEXT_FLAG_NONE);
    headerHeight=max(headerHeight,(int)size.y+6);
  }
  int x = box.x1;
  for (int c = 0; c < 3; ++c) {
    lv_area_t cell = {(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+widths[c]-1),(lv_coord_t)(y+headerHeight-1)};
    lv_draw_rect(ctx,&border,&cell); label.font=headerFont;
    cell.x1+=3; cell.x2-=3; cell.y1+=3;
    lv_draw_label(ctx,&label,&cell,headers[c],nullptr); x+=widths[c];
  }
  y += headerHeight;
  tableNextRow = 0; tableNextLine = 0;
  uint8_t row = tableRow; uint16_t line = tableLine;
  struct tm local; time_t now=time(nullptr); localtime_r(&now,&local);
  const int minute = local.tm_hour*60+local.tm_min;
  while (row < dayCourseCount) {
    String room = roomDisplayName(dayCourses[row].cell[2]);
    const char *cellText[] = {dayCourses[row].cell[0].c_str(), dayCourses[row].cell[1].c_str(), room.c_str()};
    int lines = 1;
    for (int c=0;c<3;++c) {
      lv_point_t size;
      lv_txt_get_size(&size,cellText[c],font,0,0,widths[c]-8,LV_TEXT_FLAG_NONE);
      lines=max(lines,((int)size.y+pitch-1)/pitch);
    }
    int capacity=(box.y2-y+1-6)/pitch;
    if (capacity<1) { tableNextRow=row; tableNextLine=line; break; }
    // Move an ordinary row intact to the next page; split only oversized cells.
    int fullCapacity=(lv_area_get_height(&box)-headerHeight-6)/pitch;
    int remaining=lines-line;
    if (!line && remaining>capacity && remaining<=fullCapacity && y>box.y1+headerHeight) {
      tableNextRow=row; break;
    }
    int visible=min(remaining,capacity), height=visible*pitch+6;
    x=box.x1;
    for (int c=0;c<3;++c) {
      lv_area_t cell={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+widths[c]-1),(lv_coord_t)(y+height-1)};
      border.bg_color=lv_color_hex(cfg.color[BODY]);
      border.bg_opa=dayCourses[row].minute>=minute && dayCourses[row].minute-minute<=20 ? LV_OPA_20 : LV_OPA_TRANSP;
      lv_draw_rect(ctx,&border,&cell);
      lv_area_t clip=cell; clip.x1+=4; clip.x2-=4; clip.y1+=3; clip.y2-=3;
      lv_area_t intersection; const lv_area_t *savedClip=ctx->clip_area;
      if (_lv_area_intersect(&intersection,&clip,savedClip)) {
        ctx->clip_area=&intersection;
        lv_area_t text=clip;
        // Repeat the start time on continuation pages of a very long course row.
        text.y1-=(c==0?0:line)*pitch; text.y2=text.y1+lines*pitch-1;
        label.font=font; lv_draw_label(ctx,&label,&text,cellText[c],nullptr);
        ctx->clip_area=savedClip;
      }
      x+=widths[c];
    }
    y+=height; line+=visible;
    if (line>=lines) { ++row; line=0; }
    tableNextRow=row<dayCourseCount?row:0; tableNextLine=line;
  }
}
