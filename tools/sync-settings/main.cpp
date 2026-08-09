#include "app_settings.hpp"

#if defined(_WIN32)
#include "login_image_assets.hpp"
#endif

int main() {
#if defined(_WIN32)
    ogg::assets::sync_hero_art_from_selection();
#endif
    ogg::settings::AppSettings settings{};
    ogg::settings::load(settings);
    return ogg::settings::export_for_build() ? 0 : 1;
}
