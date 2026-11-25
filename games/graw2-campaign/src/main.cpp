// GRAW 2 campaign program.

#include <rex/app/title_app.h>

#include "generated/campaign/graw2_campaign_init.h"

class CampaignApp : public rex::app::TitleApp {
 public:
  CampaignApp() : TitleApp("graw2_campaign", kPPCImageConfig) {}
};

REX_TITLE_APP(CampaignApp)
