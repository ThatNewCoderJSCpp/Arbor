#ifndef FIZMO
#define FIZMO
#endif
#ifndef FIZMO_GRAPHICS
#define FIZMO_GRAPHICS
#endif
#include <fizmo/includes.hpp>

#include <string>

bool arbor_ask_to_end(const std::string& title, const std::string& message) {
    namespace dlg = fizmo::windows::dialogs;
    return dlg::message_box(title, message, dlg::MessageButtons::YesNo, dlg::MessageIcon::Warning) == dlg::MessageResult::Yes;
}
