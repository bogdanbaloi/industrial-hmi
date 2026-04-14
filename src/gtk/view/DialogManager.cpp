#include "DialogManager.h"
#include "ThemeManager.h"

namespace app::view {

void DialogManager::showInfo(const std::string& title, 
                             const std::string& message,
                             Gtk::Window* parent) {
    marshalToMainThread([this, title, message, parent]() {
        auto* dialog = createMessageDialog(Type::INFO, title, message, parent);
        dialog->signal_response().connect([dialog](int) {
            delete dialog;
        });
        dialog->present();
    });
}

void DialogManager::showWarning(const std::string& title,
                                const std::string& message,
                                Gtk::Window* parent) {
    marshalToMainThread([this, title, message, parent]() {
        auto* dialog = createMessageDialog(Type::WARNING, title, message, parent);
        dialog->signal_response().connect([dialog](int) {
            delete dialog;
        });
        dialog->present();
    });
}

void DialogManager::showError(const std::string& title,
                              const std::string& message,
                              Gtk::Window* parent) {
    marshalToMainThread([this, title, message, parent]() {
        auto* dialog = createMessageDialog(Type::ERROR, title, message, parent);
        dialog->signal_response().connect([dialog](int) {
            delete dialog;
        });
        dialog->present();
    });
}

bool DialogManager::showConfirm(const std::string& title,
                                const std::string& message,
                                Gtk::Window* parent) {
    // Note: This is blocking, so must be called from GTK main thread
    auto* dialog = createMessageDialog(Type::QUESTION, title, message, parent);
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("OK", Gtk::ResponseType::OK);
    
    dialog->present();
    
    // Run modal loop
    auto loop = Glib::MainLoop::create();
    bool result = false;
    
    dialog->signal_response().connect([&loop, &result, dialog](int response) {
        result = (response == Gtk::ResponseType::OK);
        loop->quit();
        delete dialog;
    });
    
    loop->run();
    return result;
}

void DialogManager::showConfirmAsync(const std::string& title,
                                     const std::string& message,
                                     std::function<void(bool)> callback,
                                     Gtk::Window* parent) {
    marshalToMainThread([this, title, message, callback, parent]() {
        auto* dialog = createMessageDialog(Type::QUESTION, title, message, parent);
        dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
        dialog->add_button("OK", Gtk::ResponseType::OK);
        
        dialog->signal_response().connect([callback, dialog](int response) {
            bool confirmed = (response == Gtk::ResponseType::OK);
            callback(confirmed);
            delete dialog;
        });
        
        dialog->present();
    });
}

std::pair<bool, std::string> DialogManager::showInput(const std::string& title,
                                                      const std::string& message,
                                                      const std::string& defaultValue,
                                                      Gtk::Window* parent) {
    auto* parentWindow = getParent(parent);
    auto* dialog = parentWindow
        ? new Gtk::Dialog(title, *parentWindow)
        : new Gtk::Dialog(title);
    dialog->set_default_size(400, 150);
    dialog->set_modal(true);
    ThemeManager::instance().applyToDialog(dialog);

    // Content
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    box->set_margin(20);
    
    auto* label = Gtk::make_managed<Gtk::Label>(message);
    label->set_xalign(0);
    box->append(*label);
    
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(defaultValue);
    entry->set_hexpand(true);
    box->append(*entry);
    
    dialog->get_content_area()->append(*box);
    
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("OK", Gtk::ResponseType::OK);
    
    dialog->present();
    
    // Run modal loop
    auto loop = Glib::MainLoop::create();
    bool ok = false;
    std::string value;
    
    dialog->signal_response().connect([&](int response) {
        ok = (response == Gtk::ResponseType::OK);
        if (ok) {
            value = entry->get_text();
        }
        loop->quit();
        delete dialog;
    });
    
    loop->run();
    return {ok, value};
}

std::pair<bool, std::vector<std::string>> 
DialogManager::showForm(const std::string& title,
                       const std::vector<std::pair<std::string, std::string>>& fields,
                       Gtk::Window* parent) {
    auto* parentWindow = getParent(parent);
    auto* dialog = parentWindow
        ? new Gtk::Dialog(title, *parentWindow)
        : new Gtk::Dialog(title);
    dialog->set_default_size(400, 100 + fields.size() * 50);
    dialog->set_modal(true);
    ThemeManager::instance().applyToDialog(dialog);


    // Grid for form fields
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(12);
    grid->set_column_spacing(12);
    grid->set_margin(20);
    
    std::vector<Gtk::Entry*> entries;
    
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& [label, defaultValue] = fields[i];
        
        auto* labelWidget = Gtk::make_managed<Gtk::Label>(label);
        labelWidget->set_xalign(0);
        grid->attach(*labelWidget, 0, i);
        
        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(defaultValue);
        entry->set_hexpand(true);
        grid->attach(*entry, 1, i);
        
        entries.push_back(entry);
    }
    
    dialog->get_content_area()->append(*grid);
    
    dialog->add_button("Cancel", Gtk::ResponseType::CANCEL);
    dialog->add_button("OK", Gtk::ResponseType::OK);
    
    dialog->present();
    
    // Run modal loop
    auto loop = Glib::MainLoop::create();
    bool ok = false;
    std::vector<std::string> values;
    
    dialog->signal_response().connect([&](int response) {
        ok = (response == Gtk::ResponseType::OK);
        if (ok) {
            for (auto* entry : entries) {
                values.push_back(entry->get_text());
            }
        }
        loop->quit();
        delete dialog;
    });
    
    loop->run();
    return {ok, values};
}

Gtk::MessageDialog* DialogManager::createMessageDialog(Type type,
                                                       const std::string& title,
                                                       const std::string& message,
                                                       Gtk::Window* parent) {
    Gtk::MessageType gtkType;
    
    switch (type) {
        case Type::INFO:
            gtkType = Gtk::MessageType::INFO;
            break;
        case Type::WARNING:
            gtkType = Gtk::MessageType::WARNING;
            break;
        case Type::ERROR:
            gtkType = Gtk::MessageType::ERROR;
            break;
        case Type::QUESTION:
            gtkType = Gtk::MessageType::QUESTION;
            break;
        default:
            gtkType = Gtk::MessageType::INFO;
    }
    
    auto buttons = (type == Type::QUESTION)
        ? Gtk::ButtonsType::NONE
        : Gtk::ButtonsType::OK;

    auto* parentWindow = getParent(parent);
    auto* dialog = parentWindow
        ? new Gtk::MessageDialog(*parentWindow, title, false, gtkType, buttons)
        : new Gtk::MessageDialog(title, false, gtkType, buttons);
    
    dialog->set_secondary_text(message);
    dialog->set_modal(true);
    ThemeManager::instance().applyToDialog(dialog);


    return dialog;
}

Gtk::Window* DialogManager::getParent(Gtk::Window* parent) {
    if (parent) {
        return parent;
    }
    
    if (defaultParent_) {
        return defaultParent_;
    }
    
    // Try to find any available window
    // This is a fallback - ideally parent should always be provided
    auto display = Gdk::Display::get_default();
    if (display) {
        // In production, would iterate through windows to find main window
        // For now, return nullptr and let GTK handle it
    }
    
    // Return nullptr - GTK will handle centering on screen
    return nullptr;
}

}  // namespace app::view
