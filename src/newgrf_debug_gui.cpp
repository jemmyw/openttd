/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_debug_gui.cpp GUIs for debugging NewGRFs. */

#include "stdafx.h"
#include <stdarg.h>
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "tunnelbridge_map.h"

#include "engine_base.h"
#include "house.h"
#include "industry.h"
#include "rail.h"
#include "station_base.h"
#include "town.h"
#include "vehicle_base.h"

#include "newgrf_airporttiles.h"
#include "newgrf_debug.h"
#include "newgrf_spritegroup.h"
#include "newgrf_station.h"
#include "newgrf_town.h"

#include "table/strings.h"

/**
 * Get the feature index related to the window number.
 * @param window_number The window to get the feature index from.
 * @return the feature index
 */
static inline uint GetFeatureIndex(uint window_number)
{
	return GB(window_number, 0, 24);
}

/**
 * Get the window number for the inspect window given a
 * feature and index.
 * @param feature The feature we want to inspect.
 * @param index   The index/identifier of the feature to inspect.
 * @return the InspectWindow (Window)Number
 */
static inline uint GetInspectWindowNumber(GrfSpecFeature feature, uint index)
{
	assert((index >> 24) == 0);
	return (feature << 24) | index;
}

/**
 * The type of a property to show. This is used to
 * provide an appropriate represenation in the GUI.
 */
enum NIType {
	NIT_INT,   ///< The property is a simple integer
	NIT_CARGO, ///< The property is a cargo
};

/** Representation of the data from a NewGRF property. */
struct NIProperty {
	const char *name;       ///< A (human readable) name for the property
	ptrdiff_t offset;       ///< Offset of the variable in the class
	byte read_size;         ///< Number of bytes (i.e. byte, word, dword etc)
	byte prop;              ///< The number of the property
	byte type;
};


/**
 * Representation of the available callbacks with
 * information on when they actually apply.
 */
struct NICallback {
	const char *name; ///< The human readable name of the callback
	ptrdiff_t offset; ///< Offset of the variable in the class
	byte read_size;   ///< The number of bytes (i.e. byte, word, dword etc) to read
	byte cb_bit;      ///< The bit that needs to be set for this callback to be enabled
	uint16 cb_id;     ///< The number of the callback
};
/** Mask to show no bit needs to be enabled for the callback. */
static const int CBM_NO_BIT = UINT8_MAX;

/** Representation on the NewGRF variables. */
struct NIVariable {
	const char *name;
	byte var;
};

/** Helper class to wrap some functionality/queries in. */
class NIHelper {
public:
	/**
	 * Is the item with the given index inspectable?
	 * @param index the index to check.
	 * @return true iff the index is inspectable.
	 */
	virtual bool IsInspectable(uint index) const = 0;

	/**
	 * Get the parent "window_number" of a given instance.
	 * @param index the instance to get the parent for.
	 * @return the parent's window_number or UINT32_MAX if there is none.
	 */
	virtual uint GetParent(uint index) const = 0;

	/**
	 * Get the instance given an index.
	 * @param index the index to get the instance for.
	 * @return the instance.
	 */
	virtual const void *GetInstance(uint index) const = 0;

	/**
	 * Get (NewGRF) specs given an index.
	 * @param index the index to get the specs for for.
	 * @return the specs.
	 */
	virtual const void *GetSpec(uint index) const = 0;

	/**
	 * Set the string parameters to write the right data for a STRINGn.
	 * @param index the index to get the string parameters for.
	 */
	virtual void SetStringParameters(uint index) const = 0;

	/**
	 * Resolve (action2) variable for a given index.
	 * @param index The (instance) index to resolve the variable for.
	 * @param var   The variable to actually resolve.
	 * @param param The varaction2 0x60+x parameter to pass.
	 * @param avail Return whether the variable is available.
	 * @return The resolved variable's value.
	 */
	virtual uint Resolve(uint index, uint var, uint param, bool *avail) const
	{
		ResolverObject ro;
		memset(&ro, 0, sizeof(ro));
		this->Resolve(&ro, index);
		return ro.GetVariable(&ro, var, param, avail);
	}

protected:
	/**
	 * Actually execute the real resolving for a given (instance) index.
	 * @param ro    The resolver object to fill with everything
	 *              needed to be able to resolve a variable.
	 * @param index The (instance) index of the to-be-resolved variable.
	 */
	virtual void Resolve(ResolverObject *ro, uint index) const {}

	/**
	 * Helper to make setting the strings easier.
	 * @param string the string to actually draw.
	 * @param index  the (instance) index for the string.
	 */
	void SetSimpleStringParameters(StringID string, uint32 index) const
	{
		SetDParam(0, string);
		SetDParam(1, index);
	}


	/**
	 * Helper to make setting the strings easier for objects at a specific tile.
	 * @param string the string to draw the object's name
	 * @param index  the (instance) index for the string.
	 * @param tile   the tile the object is at
	 */
	void SetObjectAtStringParameters(StringID string, uint32 index, TileIndex tile) const
	{
		SetDParam(0, STR_NEWGRF_INSPECT_CAPTION_OBJECT_AT);
		SetDParam(1, string);
		SetDParam(2, index);
		SetDParam(3, tile);
	}
};


/** Container for all information for a given feature. */
struct NIFeature {
	const NIProperty *properties; ///< The properties associated with this feature.
	const NICallback *callbacks;  ///< The callbacks associated with this feature.
	const NIVariable *variables;  ///< The variables associated with this feature.
	const NIHelper   *helper;     ///< The class container all helper functions.
	uint psa_size;                ///< The size of the persistent storage in indices.
	size_t psa_offset;            ///< Offset to the array in the PSA.
};

/* Load all the NewGRF debug data; externalised as it is just a huge bunch of tables. */
#include "table/newgrf_debug_data.h"

/**
 * Get the NIFeature related to the window number.
 * @param window_number The window to get the NIFeature for.
 * @return the NIFeature, or NULL is there isn't one.
 */
static inline const NIFeature *GetFeature(uint window_number)
{
	byte idx = GB(window_number, 24, 8);
	return idx < GSF_FAKE_END ? _nifeatures[idx] : NULL;
}

/**
 * Get the NIHelper related to the window number.
 * @param window_number The window to get the NIHelper for.
 * @pre GetFeature(window_number) != NULL
 * @return the NIHelper
 */
static inline const NIHelper *GetFeatureHelper(uint window_number)
{
	return GetFeature(window_number)->helper;
}


/** Widget numbers of settings window */
enum NewGRFInspectWidgets {
	NIW_CAPTION,   ///< The caption bar ofcourse
	NIW_PARENT,    ///< Inspect the parent
	NIW_MAINPANEL, ///< Panel widget containing the actual data
	NIW_SCROLLBAR, ///< Scrollbar
};

/** Window used for inspecting NewGRFs. */
struct NewGRFInspectWindow : Window {
	static const int LEFT_OFFSET   = 5; ///< Position of left edge
	static const int RIGHT_OFFSET  = 5; ///< Position of right edge
	static const int TOP_OFFSET    = 5; ///< Position of top edge
	static const int BOTTOM_OFFSET = 5; ///< Position of bottom edge

	/** The value for the variable 60 parameters. */
	static byte var60params[0x20];

	/** The currently editted parameter, to update the right one. */
	byte current_edit_param;

	/**
	 * Check whether the given variable has a parameter.
	 * @param variable the variable to check.
	 * @return true iff the variable has a parameter.
	 */
	static bool HasVariableParameter(uint variable)
	{
		return IsInsideBS(variable, 0x60, 0x20);
	}

	NewGRFInspectWindow(const WindowDesc *desc, WindowNumber wno) : Window()
	{
		this->InitNested(desc, wno);

		this->vscroll.SetCount(0);
		this->SetWidgetDisabledState(NIW_PARENT, GetFeatureHelper(this->window_number)->GetParent(GetFeatureIndex(this->window_number)) == UINT32_MAX);
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget != NIW_CAPTION) return;

		GetFeatureHelper(this->window_number)->SetStringParameters(GetFeatureIndex(this->window_number));
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget != NIW_MAINPANEL) return;

		resize->height = max(11, FONT_HEIGHT_NORMAL + 1);
		resize->width  = 1;

		size->height = 5 * resize->height + TOP_OFFSET + BOTTOM_OFFSET;
	}

	/**
	 * Helper function to draw a string (line) in the window.
	 * @param r      The (screen) rectangle we must draw within
	 * @param offset The offset (in lines) we want to draw for
	 * @param format The format string
	 */
	void WARN_FORMAT(4, 5) DrawString(const Rect &r, int offset, const char *format, ...) const
	{
		char buf[1024];

		va_list va;
		va_start(va, format);
		vsnprintf(buf, lengthof(buf), format, va);
		va_end(va);

		offset -= this->vscroll.GetPosition();
		if (offset < 0 || offset >= this->vscroll.GetCapacity()) return;

		::DrawString(r.left + LEFT_OFFSET, r.right + RIGHT_OFFSET, r.top + TOP_OFFSET + (offset * this->resize.step_height), buf, TC_BLACK);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != NIW_MAINPANEL) return;

		uint index = GetFeatureIndex(this->window_number);
		const NIFeature *nif  = GetFeature(this->window_number);
		const NIHelper *nih   = nif->helper;
		const void *base      = nih->GetInstance(index);
		const void *base_spec = nih->GetSpec(index);

		uint i = 0;
		if (nif->variables != NULL) {
			this->DrawString(r, i++, "Variables:");
			for (const NIVariable *niv = nif->variables; niv->name != NULL; niv++) {
				bool avail = true;
				uint param = HasVariableParameter(niv->var) ? NewGRFInspectWindow::var60params[niv->var - 0x60] : 0;
				uint value = nih->Resolve(index, niv->var, param, &avail);

				if (!avail) continue;

				if (HasVariableParameter(niv->var)) {
					this->DrawString(r, i++, "  %02x[%02x]: %08x (%s)", niv->var, param, value, niv->name);
				} else {
					this->DrawString(r, i++, "  %02x: %08x (%s)", niv->var, value, niv->name);
				}
			}
		}

		if (nif->psa_size != 0) {
			this->DrawString(r, i++, "Persistent storage:");
			assert(nif->psa_size % 4 == 0);
			int32 *psa = (int32*)((byte*)base + nif->psa_offset);
			for (uint j = 0; j < nif->psa_size; j += 4, psa += 4) {
				this->DrawString(r, i++, "  %i: %i %i %i %i", j, psa[0], psa[1], psa[2], psa[3]);
			}
		}

		if (nif->properties != NULL) {
			this->DrawString(r, i++, "Properties:");
			for (const NIProperty *nip = nif->properties; nip->name != NULL; nip++) {
				void *ptr = (byte*)base + nip->offset;
				uint value;
				switch (nip->read_size) {
					case 1: value = *(uint8  *)ptr; break;
					case 2: value = *(uint16 *)ptr; break;
					case 4: value = *(uint32 *)ptr; break;
					default: NOT_REACHED();
				}

				StringID string;
				SetDParam(0, value);
				switch (nip->type) {
					case NIT_INT:
						string = STR_JUST_INT;
						break;

					case NIT_CARGO:
						string = value != INVALID_CARGO ? CargoSpec::Get(value)->name : STR_QUANTITY_N_A;
						break;

					default:
						NOT_REACHED();
				}

				char buffer[64];
				GetString(buffer, string, lastof(buffer));
				this->DrawString(r, i++, "  %02x: %s (%s)", nip->prop, buffer, nip->name);
			}
		}

		if (nif->callbacks != NULL) {
			this->DrawString(r, i++, "Callbacks:");
			for (const NICallback *nic = nif->callbacks; nic->name != NULL; nic++) {
				if (nic->cb_bit != CBM_NO_BIT) {
					void *ptr = (byte*)base_spec + nic->offset;
					uint value;
					switch (nic->read_size) {
						case 1: value = *(uint8  *)ptr; break;
						case 2: value = *(uint16 *)ptr; break;
						case 4: value = *(uint32 *)ptr; break;
						default: NOT_REACHED();
					}

					if (!HasBit(value, nic->cb_bit)) continue;
					this->DrawString(r, i++, "  %03x: %s", nic->cb_id, nic->name);
				} else {
					this->DrawString(r, i++, "  %03x: %s (unmasked)", nic->cb_id, nic->name);
				}
			}
		}

		/* Not nice and certainly a hack, but it beats duplicating
		 * this whole function just to count the actual number of
		 * elements. Especially because they need to be redrawn. */
		const_cast<NewGRFInspectWindow*>(this)->vscroll.SetCount(i);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case NIW_PARENT: {
				uint index = GetFeatureHelper(this->window_number)->GetParent(GetFeatureIndex(this->window_number));
				::ShowNewGRFInspectWindow((GrfSpecFeature)GB(index, 24, 8), GetFeatureIndex(index));
			} break;

			case NIW_MAINPANEL: {
				/* Does this feature have variables? */
				const NIFeature *nif  = GetFeature(this->window_number);
				if (nif->variables == NULL) return;

				/* Get the line, make sure it's within the boundaries. */
				int line = (pt.y - this->GetWidget<NWidgetBase>(NIW_MAINPANEL)->pos_y - TOP_OFFSET) / this->resize.step_height;
				if (line >= this->vscroll.GetCapacity()) return;
				line += this->vscroll.GetPosition();

				/* Find the variable related to the line */
				for (const NIVariable *niv = nif->variables; niv->name != NULL; niv++, line--) {
					if (line != 1) continue; // 1 because of the "Variables:" line

					if (!HasVariableParameter(niv->var)) break;

					this->current_edit_param = niv->var;
					ShowQueryString(STR_EMPTY, STR_NEWGRF_INSPECT_QUERY_CAPTION, 3, 100, this, CS_HEXADECIMAL, QSF_NONE);
				}
			}
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (StrEmpty(str)) return;

		NewGRFInspectWindow::var60params[this->current_edit_param - 0x60] = strtol(str, NULL, 16);
		this->SetDirty();
	}

	virtual void OnResize()
	{
		this->vscroll.SetCapacityFromWidget(this, NIW_MAINPANEL, TOP_OFFSET + BOTTOM_OFFSET);
	}
};

/* static */ byte NewGRFInspectWindow::var60params[0x20] = { 0 }; // Use spec to have 0s in whole array

static const NWidgetPart _nested_newgrf_inspect_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, NIW_CAPTION), SetDataTip(STR_NEWGRF_INSPECT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, NIW_PARENT), SetDataTip(STR_NEWGRF_INSPECT_PARENT_BUTTON, STR_NEWGRF_INSPECT_PARENT_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, NIW_MAINPANEL), SetMinimalSize(300, 0), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_SCROLLBAR, COLOUR_GREY, NIW_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static const WindowDesc _newgrf_inspect_desc(
	WDP_AUTO, 400, 300,
	WC_NEWGRF_INSPECT, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_newgrf_inspect_widgets, lengthof(_nested_newgrf_inspect_widgets)
);

void ShowNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (!IsNewGRFInspectable(feature, index)) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	AllocateWindowDescFront<NewGRFInspectWindow>(&_newgrf_inspect_desc, wno);
}

void DeleteNewGRFInspectWindow(GrfSpecFeature feature, uint index)
{
	if (feature == GSF_INVALID) return;

	WindowNumber wno = GetInspectWindowNumber(feature, index);
	DeleteWindowById(WC_NEWGRF_INSPECT, wno);

	/* Reinitialise the land information window to remove the "debug" sprite if needed. */
	Window *w = FindWindowById(WC_LAND_INFO, 0);
	if (w != NULL) w->ReInit();
}

bool IsNewGRFInspectable(GrfSpecFeature feature, uint index)
{
	const NIFeature *nif = GetFeature(GetInspectWindowNumber(feature, index));
	if (nif == NULL) return false;
	return nif->helper->IsInspectable(index);
}

GrfSpecFeature GetGrfSpecFeature(TileIndex tile)
{
	switch (GetTileType(tile)) {
		default:              return GSF_INVALID;
		case MP_RAILWAY:      return GSF_RAILTYPES;
		case MP_ROAD:         return IsLevelCrossing(tile) ? GSF_RAILTYPES : GSF_INVALID;
		case MP_HOUSE:        return GSF_HOUSES;
		case MP_INDUSTRY:     return GSF_INDUSTRYTILES;

		case MP_STATION:
			switch (GetStationType(tile)) {
				case STATION_RAIL:    return GSF_STATIONS;
				case STATION_AIRPORT: return GSF_AIRPORTTILES;
				default:              return GSF_INVALID;
			}
	}
}

GrfSpecFeature GetGrfSpecFeature(VehicleType type)
{
	switch (type) {
		case VEH_TRAIN:    return GSF_TRAINS;
		case VEH_ROAD:     return GSF_ROADVEHICLES;
		case VEH_SHIP:     return GSF_SHIPS;
		case VEH_AIRCRAFT: return GSF_AIRCRAFT;
		default:           return GSF_INVALID;
	}
}