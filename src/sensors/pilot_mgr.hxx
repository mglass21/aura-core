/**
 * \file: pilot_mgr.hxx
 *
 * Front end management interface for reading pilot input.
 *
 * Copyright (C) 2010 - Curtis L. Olson curtolson@flightgear.org
 *
 */

#pragma once

void PilotInput_init();
bool PilotInput_update();
void PilotInput_close();
