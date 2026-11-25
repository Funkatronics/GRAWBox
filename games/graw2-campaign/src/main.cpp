// GRAW 2 campaign program.

#include <rex/app/title_app.h>

#include "native_host.h"

#include "generated/campaign/graw2_campaign_init.h"

class CampaignApp : public rex::app::TitleApp {
 public:
  CampaignApp() : TitleApp("graw2_campaign", kPPCImageConfig) { grawbox::SetApp(this); }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    if (auto result = settings.Register(grawbox::Settings()); !result) return result;
    return TitleApp::OnConfigure(settings);
  }
};

REX_TITLE_APP(CampaignApp)
