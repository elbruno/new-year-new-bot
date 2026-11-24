// Goal: Show how to supply an optional local development setup password.
// Responsibilities: Provide an intentionally invalid example, not a real key.
// How it works:
// 1. Copy the example to the ignored private header.
// 2. Replace the placeholder locally with an appropriate development value.
// 3. Keep the real header and local-key binaries out of public material.
#pragma once

// Example only. Copy and edit this line in the ignored wifi_setup_private.h
// file for local development. This placeholder is intentionally invalid.
#define NETBOT_DEV_SETUP_PASSWORD "<EDIT-PRIVATE-8-CHAR-KEY>"

// The real header and any local-key firmware binary are private and must remain
// outside source control, logs and public/release material.
