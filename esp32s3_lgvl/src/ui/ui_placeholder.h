/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_PLACEHOLDER_H_
#define APP_UI_PLACEHOLDER_H_

void ui_placeholder_screen_create(void);

/* Sets the "coming soon" title to feature_name and pushes the screen.
 * Shared by every main-menu item not yet implemented (Shell, Radio,
 * About in Phase 1) so each doesn't need its own throwaway screen.
 */
void ui_placeholder_screen_enter(const char *feature_name);

#endif /* APP_UI_PLACEHOLDER_H_ */
