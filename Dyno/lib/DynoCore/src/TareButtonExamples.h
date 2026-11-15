// The tare-button settings the host tests are written against (#36). They
// equal today's DynoConfig.h/pindefs.h values (test_config checks this), but
// are kept here so the tests' hand-worked press times don't silently change
// meaning when the real settings are retuned.
#pragma once

#include "TareButton.h"

// Active-low (pull-up, button to ground); a press counts after 50 ms.
static const TareButtonConfig kExampleTareButtonConfig = {true, 50};
