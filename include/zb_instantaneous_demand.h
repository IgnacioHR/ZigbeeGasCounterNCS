/*
 * Zigbee Gas Counter - An open-source Zigbee gas counter project.
 * Copyright (c) 2026 Ignacio Hernández-Ros.
 *
 * This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0
 * International License. To view a copy of this license, visit
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * You may use, modify, and share this work for personal and non-commercial purposes, as long
 * as you credit the original author(s) and share any derivatives under the same license.
 */

#pragma once

#ifndef ZB_INSTANTANEOUS_DEMAND_H
#define ZB_INSTANTANEOUS_DEMAND_H

#ifdef FEATURE_MEASURE_FLOW_RATE

void zb_compute_instantaneous_demand();

#endif // FEATURE_MEASURE_FLOW_RATE

#endif // ZB_INSTANTANEOUS_DEMAND_H
