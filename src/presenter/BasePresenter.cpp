#include "BasePresenter.h"
#include <algorithm>

namespace app {

BasePresenter::~BasePresenter() {
    // Ensure all observers are removed
    std::lock_guard<std::mutex> lock(observersMutex_);
    observers_.clear();
}

void BasePresenter::addObserver(ViewObserver* observer) {
    if (!observer) return;
    
    std::lock_guard<std::mutex> lock(observersMutex_);
    
    // Avoid duplicates
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void BasePresenter::removeObserver(ViewObserver* observer) {
    if (!observer) return;
    
    std::lock_guard<std::mutex> lock(observersMutex_);
    observers_.erase(
        std::remove(observers_.begin(), observers_.end(), observer),
        observers_.end()
    );
}

}  // namespace app
