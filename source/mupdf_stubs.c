#include <time.h>
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

time_t timegm(struct tm *tm)
{
    return mktime(tm);
}

hb_buffer_t *fzhb_buffer_create(void)
{
    return hb_buffer_create();
}

void fzhb_buffer_destroy(hb_buffer_t *buf)
{
    hb_buffer_destroy(buf);
}

void fzhb_buffer_clear_contents(hb_buffer_t *buf)
{
    hb_buffer_clear_contents(buf);
}

void fzhb_buffer_set_direction(hb_buffer_t *buf, hb_direction_t dir)
{
    hb_buffer_set_direction(buf, dir);
}

void fzhb_buffer_set_language(hb_buffer_t *buf, hb_language_t lang)
{
    hb_buffer_set_language(buf, lang);
}

void fzhb_buffer_set_cluster_level(hb_buffer_t *buf, hb_buffer_cluster_level_t level)
{
    hb_buffer_set_cluster_level(buf, level);
}

void fzhb_buffer_add_utf8(hb_buffer_t *buf, const char *text, int text_length,
                          unsigned int item_offset, int item_length)
{
    hb_buffer_add_utf8(buf, text, text_length, item_offset, item_length);
}

void fzhb_buffer_guess_segment_properties(hb_buffer_t *buf)
{
    hb_buffer_guess_segment_properties(buf);
}

void fzhb_shape(hb_font_t *font, hb_buffer_t *buf,
                const hb_feature_t *features, unsigned int num_features)
{
    hb_shape(font, buf, features, num_features);
}

hb_glyph_position_t *fzhb_buffer_get_glyph_positions(hb_buffer_t *buf,
                                                      unsigned int *length)
{
    return hb_buffer_get_glyph_positions(buf, length);
}

hb_glyph_info_t *fzhb_buffer_get_glyph_infos(hb_buffer_t *buf,
                                              unsigned int *length)
{
    return hb_buffer_get_glyph_infos(buf, length);
}

hb_language_t fzhb_language_from_string(const char *str, int len)
{
    return hb_language_from_string(str, len);
}

hb_font_t *fzhb_ft_font_create(FT_Face ft_face, hb_destroy_func_t destroy)
{
    return hb_ft_font_create(ft_face, destroy);
}

void fzhb_font_destroy(hb_font_t *font)
{
    hb_font_destroy(font);
}
