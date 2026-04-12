/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "engines/grim/debug.h"
#include "engines/grim/set.h"
#include "engines/grim/textsplit.h"
#include "engines/grim/colormap.h"
#include "engines/grim/grim.h"
#include "engines/grim/savegame.h"
#include "engines/grim/resource.h"
#include "engines/grim/bitmap.h"
#include "engines/grim/gfx_base.h"
#include "engines/grim/sound.h"
#include "engines/grim/emi/sound/emisound.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/formats/json.h"
#include "math/frustum.h"
#include "graphics/surface.h"
#include "image/png.h"

namespace Grim {

namespace {

static void destroyLayeredParallaxBackdrop(Set::LayeredParallaxBackdrop *backdrop) {
	if (!backdrop)
		return;

	delete backdrop->baseBitmap;
	for (uint i = 0; i < backdrop->layers.size(); ++i) {
		delete backdrop->layers[i].bitmap;
	}
	delete backdrop;
}

static float jsonNumberOrDefault(const Common::JSONValue *value, float fallback) {
	if (!value)
		return fallback;
	if (value->isIntegerNumber())
		return (float)value->asIntegerNumber();
	if (value->isNumber())
		return (float)value->asNumber();
	return fallback;
}

static int jsonIntOrDefault(const Common::JSONValue *value, int fallback) {
	if (!value)
		return fallback;
	if (value->isIntegerNumber())
		return (int)value->asIntegerNumber();
	if (value->isNumber())
		return (int)value->asNumber();
	return fallback;
}

static Bitmap *loadBitmapFromPng(const Common::Path &path) {
	Common::FSNode node(path);
	if (!node.exists() || node.isDirectory()) {
		warning("Layered parallax PNG missing: %s", path.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	Common::File file;
	if (!file.open(node))
		return nullptr;

	Image::PNGDecoder decoder;
	if (!decoder.loadStream(file) || !decoder.getSurface()) {
		warning("Failed to decode layered parallax PNG: %s", path.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	Graphics::Surface surfaceCopy;
	surfaceCopy.copyFrom(*decoder.getSurface());
	Bitmap *bitmap = new Bitmap(surfaceCopy, surfaceCopy.w, surfaceCopy.h, path.toString().c_str());
	surfaceCopy.free();
	return bitmap;
}

static Set::LayeredParallaxBackdrop *loadLayeredParallaxBackdropManifest(const Common::Path &manifestPath) {
	Common::FSNode manifestNode(manifestPath);
	if (!manifestNode.exists() || manifestNode.isDirectory())
		return nullptr;

	Common::File file;
	if (!file.open(manifestNode))
		return nullptr;

	const int64 manifestSize = file.size();
	if (manifestSize <= 0 || manifestSize > 16 * 1024 * 1024) {
		warning("Layered parallax manifest has invalid size: %s", manifestPath.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	char *jsonBytes = new char[(uint32)manifestSize + 1];
	file.read(jsonBytes, (uint32)manifestSize);
	jsonBytes[manifestSize] = 0;
	Common::JSONValue *root = Common::JSON::parse(jsonBytes);
	delete[] jsonBytes;

	if (!root || !root->isObject()) {
		delete root;
		warning("Layered parallax manifest parse failed: %s", manifestPath.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	Set::LayeredParallaxBackdrop *backdrop = new Set::LayeredParallaxBackdrop();
	backdrop->manifestPath = manifestPath.toString(Common::Path::kNativeSeparator);

	const Common::JSONObject &obj = root->asObject();
	const Common::JSONValue *originValue = obj.contains("origin") ? obj["origin"] : nullptr;
	if (originValue && originValue->isObject()) {
		const Common::JSONObject &origin = originValue->asObject();
		backdrop->originX = jsonIntOrDefault(origin.contains("x") ? origin["x"] : nullptr, 0);
		backdrop->originY = jsonIntOrDefault(origin.contains("y") ? origin["y"] : nullptr, 0);
	}

	const Common::JSONValue *baseValue = obj.contains("base") ? obj["base"] : nullptr;
	if (!baseValue || !baseValue->isObject()) {
		delete root;
		destroyLayeredParallaxBackdrop(backdrop);
		warning("Layered parallax manifest is missing a base plate: %s", manifestPath.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	const Common::JSONObject &base = baseValue->asObject();
	if (!base.contains("image") || !base["image"]->isString()) {
		delete root;
		destroyLayeredParallaxBackdrop(backdrop);
		warning("Layered parallax base plate has no image path: %s", manifestPath.toString(Common::Path::kNativeSeparator).c_str());
		return nullptr;
	}

	backdrop->baseFactor = jsonNumberOrDefault(base.contains("factor") ? base["factor"] : nullptr, 0.65f);
	backdrop->baseBitmap = loadBitmapFromPng(manifestPath.getParent().appendComponent(base["image"]->asString()));
	if (!backdrop->baseBitmap) {
		delete root;
		destroyLayeredParallaxBackdrop(backdrop);
		return nullptr;
	}

	const Common::JSONValue *layersValue = obj.contains("layers") ? obj["layers"] : nullptr;
	if (layersValue && layersValue->isArray()) {
		const Common::JSONArray &layers = layersValue->asArray();
		for (uint i = 0; i < layers.size(); ++i) {
			if (!layers[i] || !layers[i]->isObject())
				continue;

			const Common::JSONObject &layerObject = layers[i]->asObject();
			if (!layerObject.contains("image") || !layerObject["image"]->isString())
				continue;

			Set::LayeredParallaxLayer layer;
			layer.name = layerObject.contains("name") && layerObject["name"]->isString() ? layerObject["name"]->asString() : Common::String::format("layer_%u", i);
			layer.factor = jsonNumberOrDefault(layerObject.contains("factor") ? layerObject["factor"] : nullptr, 1.0f);
			layer.bitmap = loadBitmapFromPng(manifestPath.getParent().appendComponent(layerObject["image"]->asString()));
			if (!layer.bitmap)
				continue;

			backdrop->layers.push_back(layer);
		}
	}

	delete root;
	return backdrop;
}

static void addUniqueManifestCandidate(Common::Array<Common::Path> &manifestCandidates, const Common::Path &candidate) {
	const Common::String candidateString = candidate.toString(Common::Path::kNativeSeparator);
	for (uint i = 0; i < manifestCandidates.size(); ++i) {
		if (manifestCandidates[i].toString(Common::Path::kNativeSeparator).equalsIgnoreCase(candidateString))
			return;
	}

	manifestCandidates.push_back(candidate);
}

static bool isNumberedSetupAlias(const Common::String &directoryName, const Common::String &setupName) {
	const uint32 setupSep = setupName.find('_');
	if (setupSep == Common::String::npos)
		return false;

	const Common::String prefix = setupName.substr(0, setupSep);
	const Common::String suffix = setupName.substr(setupSep + 1);
	if (prefix.empty() || suffix.empty())
		return false;

	if (!directoryName.hasPrefixIgnoreCase(prefix + "_"))
		return false;

	const Common::String remainder = directoryName.substr(prefix.size() + 1);
	const uint32 remainderSep = remainder.find('_');
	if (remainderSep == Common::String::npos)
		return false;

	const Common::String numericPart = remainder.substr(0, remainderSep);
	const Common::String aliasedSuffix = remainder.substr(remainderSep + 1);
	if (numericPart.empty() || aliasedSuffix.empty() || !aliasedSuffix.equalsIgnoreCase(suffix))
		return false;

	for (uint i = 0; i < numericPart.size(); ++i) {
		if (!Common::isDigit(numericPart[i]))
			return false;
	}

	return true;
}

static void addManifestCandidatesForRoot(Common::Array<Common::Path> &manifestCandidates, const Common::Path &rootPath, const Common::String &setupName) {
	addUniqueManifestCandidate(manifestCandidates, rootPath.appendComponent(setupName).appendComponent("manifest.json"));

	Common::FSNode rootNode(rootPath);
	if (!rootNode.exists() || !rootNode.isDirectory())
		return;

	Common::FSList children;
	if (!rootNode.getChildren(children, Common::FSNode::kListDirectoriesOnly))
		return;

	for (Common::FSList::const_iterator it = children.begin(); it != children.end(); ++it) {
		if (!isNumberedSetupAlias((*it).getName(), setupName))
			continue;
		addUniqueManifestCandidate(manifestCandidates, (*it).getPath().appendComponent("manifest.json"));
	}
}

static Set::LayeredParallaxBackdrop *loadLayeredParallaxBackdropForSetup(const Set::Setup &setup) {
	if (setup._parallaxBackdropLoadAttempted)
		return setup._parallaxBackdrop;

	setup._parallaxBackdropLoadAttempted = true;
	setup._parallaxBackdropResolvedManifestPath.clear();
	setup._parallaxBackdropLoadStatus = "not_found";

	const Common::Path gamePath = ConfMan.getPath("path");
	Common::FSNode gamePathNode(gamePath);
	Common::Array<Common::Path> manifestCandidates;
	addManifestCandidatesForRoot(manifestCandidates, gamePath.appendComponent("parallax_layers"), setup._name);
	addManifestCandidatesForRoot(manifestCandidates, gamePath.appendComponent("GRIMDATA").appendComponent("parallax_layers"), setup._name);
	addManifestCandidatesForRoot(manifestCandidates, gamePath.getParent().appendComponent("parallax_layers"), setup._name);
	if (gamePathNode.exists() && gamePathNode.isDirectory()) {
		const Common::FSNode parentNode = gamePathNode.getParent();
		if (parentNode.exists() && parentNode.isDirectory())
			addManifestCandidatesForRoot(manifestCandidates, parentNode.getPath().appendComponent("parallax_layers"), setup._name);
	}

	for (uint i = 0; i < manifestCandidates.size(); ++i) {
		Common::FSNode manifestNode(manifestCandidates[i]);
		if (!manifestNode.exists() || manifestNode.isDirectory())
			continue;

		setup._parallaxBackdropResolvedManifestPath = manifestCandidates[i].toString(Common::Path::kNativeSeparator);
		setup._parallaxBackdropLoadStatus = "load_failed";
		setup._parallaxBackdrop = loadLayeredParallaxBackdropManifest(manifestCandidates[i]);
		if (setup._parallaxBackdrop) {
			setup._parallaxBackdropLoadStatus = "loaded";
			return setup._parallaxBackdrop;
		}
	}

	return nullptr;
}

} // namespace

Set::Set(const Common::String &sceneName, Common::SeekableReadStream *data) :
		_locked(false), _name(sceneName), _enableLights(false) {

	char header[7];
	data->read(header, 7);
	data->seek(0, SEEK_SET);
	if (memcmp(header, "section", 7) == 0) {
		TextSplitter ts(_name, data);
		loadText(ts);
	} else {
		loadBinary(data);
	}
	setupOverworldLights();
}

Set::Set() :
		_cmaps(nullptr), _locked(false), _enableLights(false), _numSetups(0),
		_numLights(0), _numSectors(0), _numObjectStates(0), _minVolume(0),
		_maxVolume(0), _numCmaps(0), _numShadows(0), _currSetup(nullptr),
		_setups(nullptr), _lights(nullptr), _sectors(nullptr), _shadows(nullptr) {
	setupOverworldLights();
}

Set::~Set() {
	if (_cmaps || g_grim->getGameType() == GType_MONKEY4) {
		delete[] _cmaps;
		for (int i = 0; i < _numSetups; ++i) {
			destroyLayeredParallaxBackdrop(_setups[i]._parallaxBackdrop);
			delete _setups[i]._bkgndBm;
			delete _setups[i]._bkgndZBm;
		}
		delete[] _setups;
		turnOffLights();
		delete[] _lights;
		for (int i = 0; i < _numSectors; ++i) {
			delete _sectors[i];
		}
		delete[] _sectors;
		while (!_states.empty()) {
			ObjectState *s = _states.front();
			_states.pop_front();
			delete s;
		}
		delete[] _shadows;
	}
	for (Light *l : _overworldLightsList) {
		delete l;
	}
}

void Set::setupOverworldLights() {
	Light *l;

	l = new Light();
	l->_name = "Overworld Light 1";
	l->_enabled = true;
	l->_type = Light::Ambient;
	l->_pos = Math::Vector3d(0, 0, 0);
	l->_dir = Math::Vector3d(0, 0, 0);
	l->_color = Color(255, 255, 255);
	l->setIntensity(0.5f);
	_overworldLightsList.push_back(l);

	l = new Light();
	l->_name = "Overworld Light 2";
	l->_enabled = true;
	l->_type = Light::Direct;
	l->_pos = Math::Vector3d(0, 0, 0);
	l->_dir = Math::Vector3d(0, 0, -1);
	l->_color = Color(255, 255, 255);
	l->setIntensity(0.6f);
	_overworldLightsList.push_back(l);
}

void Set::loadText(TextSplitter &ts) {
	char tempBuf[256];

	ts.expectString("section: colormaps");
	ts.scanString(" numcolormaps %d", 1, &_numCmaps);
	_cmaps = new ObjectPtr<CMap>[_numCmaps];
	char cmap_name[256];
	for (int i = 0; i < _numCmaps; i++) {
		ts.scanString(" colormap %256s", 1, cmap_name);
		_cmaps[i] = g_resourceloader->getColormap(cmap_name);
	}

	if (ts.checkString("section: objectstates") || ts.checkString("sections: object_states")) {
		ts.nextLine();
		ts.scanString(" tot_objects %d", 1, &_numObjectStates);
		char object_name[256];
		for (int l = 0; l < _numObjectStates; l++) {
			ts.scanString(" object %256s", 1, object_name);
		}
	} else {
		_numObjectStates = 0;
	}

	ts.expectString("section: setups");
	ts.scanString(" numsetups %d", 1, &_numSetups);
	_setups = new Setup[_numSetups];
	for (int i = 0; i < _numSetups; i++)
		_setups[i].load(this, i, ts);
	_currSetup = _setups;

	_numShadows = 0;
	_numSectors = -1;
	_numLights = -1;
	_lights = nullptr;
	_sectors = nullptr;
	_shadows = nullptr;

	_minVolume = 0;
	_maxVolume = 0;

	// Lights are optional
	if (ts.isEof())
		return;

	ts.expectString("section: lights");
	ts.scanString(" numlights %d", 1, &_numLights);
	_lights = new Light[_numLights];
	for (int i = 0; i < _numLights; i++) {
		_lights[i].load(ts);
		_lights[i]._id = i;
		_lightsList.push_back(&_lights[i]);
	}

	// Calculate the number of sectors
	ts.expectString("section: sectors");
	if (ts.isEof()) // Sectors are optional, but section: doesn't seem to be
		return;

	int sectorStart = ts.getLineNumber();
	_numSectors = 0;
	// Find the number of sectors (while the sectors usually
	// count down from the highest number there are a few
	// cases where they count up, see hh.set for example)
	while (!ts.isEof()) {
		ts.scanString(" %s", 1, tempBuf);
		if (!scumm_stricmp(tempBuf, "sector"))
			_numSectors++;
	}
	// Allocate and fill an array of sector info
	_sectors = new Sector*[_numSectors];
	ts.setLineNumber(sectorStart);
	for (int i = 0; i < _numSectors; i++) {
		// Use the ids as index for the sector in the array.
		// This way when looping they are checked from the id 0 sto the last,
		// which seems important for sets with overlapping camera sectors, like ga.set.
		Sector *s = new Sector();
		s->load(ts);
		_sectors[s->getSectorId()] = s;
	}
}

void Set::loadBinary(Common::SeekableReadStream *data) {
	// yes, an array of size 0
	_cmaps = nullptr;//new CMapPtr[0];
	_numCmaps = 0;
	_numObjectStates = 0;


	_numSetups = data->readUint32LE();
	_setups = new Setup[_numSetups];
	for (int i = 0; i < _numSetups; i++)
		_setups[i].loadBinary(data);
	_currSetup = _setups;

	_numSectors = 0;
	_numLights = 0;
	_lights = nullptr;
	_sectors = nullptr;
	_shadows = nullptr;

	_minVolume = 0;
	_maxVolume = 0;

	// the rest may or may not be optional. Might be a good idea to check if there is no more data.

	_numLights = data->readUint32LE();
	_lights = new Light[_numLights];
	for (int i = 0; i < _numLights; i++) {
		_lights[i].loadBinary(data);
		_lights[i]._id = i;
		_lightsList.push_back(&_lights[i]);
	}

	_numSectors = data->readUint32LE();
	// Allocate and fill an array of sector info
	_sectors = new Sector*[_numSectors];
	for (int i = 0; i < _numSectors; i++) {
		_sectors[i] = new Sector();
		_sectors[i]->loadBinary(data);
	}

	_numShadows = data->readUint32LE();
	_shadows = new SetShadow[_numShadows];

	for (int i = 0; i < _numShadows; ++i) {
		_shadows[i].loadBinary(data, this);
	}

	// Enable lights by default
	_enableLights = true;
}

void Set::saveState(SaveGame *savedState) const {
	savedState->writeString(_name);
	if (g_grim->getGameType() == GType_GRIM) {
		savedState->writeLESint32(_numCmaps);
		for (int i = 0; i < _numCmaps; ++i) {
			savedState->writeString(_cmaps[i]->getFilename());
		}
	}
	savedState->writeLEUint32((uint32)(_currSetup - _setups)); // current setup id
	savedState->writeBool(_locked);
	savedState->writeBool(_enableLights);
	savedState->writeLESint32(_minVolume);
	savedState->writeLESint32(_maxVolume);

	savedState->writeLEUint32(_states.size());
	for (StateList::const_iterator i = _states.begin(); i != _states.end(); ++i) {
		savedState->writeLESint32((*i)->getId());
	}

	// Setups
	savedState->writeLESint32(_numSetups);
	for (int i = 0; i < _numSetups; ++i) {
		_setups[i].saveState(savedState);
	}

	// Sectors
	savedState->writeLESint32(_numSectors);
	for (int i = 0; i < _numSectors; ++i) {
		_sectors[i]->saveState(savedState);
	}

	// Lights
	savedState->writeLESint32(_numLights);
	for (int i = 0; i < _numLights; ++i) {
		_lights[i].saveState(savedState);
	}

	// Shadows
	savedState->writeLESint32(_numShadows);
	for (int i = 0; i < _numShadows; ++i) {
		_shadows[i].saveState(savedState);
	}
}

bool Set::restoreState(SaveGame *savedState) {
	_name = savedState->readString();
	if (g_grim->getGameType() == GType_GRIM) {
		_numCmaps = savedState->readLESint32();
		_cmaps = new CMapPtr[_numCmaps];
		for (int i = 0; i < _numCmaps; ++i) {
			Common::String str = savedState->readString();
			_cmaps[i] = g_resourceloader->getColormap(str);
		}
	}

	int32 currSetupId = savedState->readLEUint32();
	_locked           = savedState->readBool();
	_enableLights     = savedState->readBool();
	_minVolume        = savedState->readLESint32();
	_maxVolume        = savedState->readLESint32();

	_numObjectStates = savedState->readLESint32();
	_states.clear();
	for (int i = 0; i < _numObjectStates; ++i) {
		int32 id = savedState->readLESint32();
		ObjectState *o = ObjectState::getPool().getObject(id);
		_states.push_back(o);
	}

	//Setups
	_numSetups = savedState->readLESint32();
	_setups = new Setup[_numSetups];
	_currSetup = _setups + currSetupId;
	for (int i = 0; i < _numSetups; ++i) {
		_setups[i].restoreState(savedState);
	}

	//Sectors
	_numSectors = savedState->readLESint32();
	if (_numSectors > 0) {
		_sectors = new Sector*[_numSectors];
		for (int i = 0; i < _numSectors; ++i) {
			_sectors[i] = new Sector();
			_sectors[i]->restoreState(savedState);
		}
	} else {
		_sectors = nullptr;
	}

	_numLights = savedState->readLESint32();
	_lights = new Light[_numLights];
	for (int i = 0; i < _numLights; i++) {
		_lights[i].restoreState(savedState);
		_lights[i]._id = i;
		_lightsList.push_back(&_lights[i]);
	}

	if (savedState->saveMinorVersion() >= 19) {
		_numShadows = savedState->readLESint32();
		_shadows = new SetShadow[_numShadows];
		for (int i = 0; i < _numShadows; ++i) {
			_shadows[i].restoreState(savedState);
		}
	}

	return true;
}

void Set::Setup::load(Set *set, int id, TextSplitter &ts) {
	char buf[256];

	ts.scanString(" setup %256s", 1, buf);
	_name = buf;

	ts.scanString(" background %256s", 1, buf);
	_bkgndBm = loadBackground(buf);

	// ZBuffer is optional
	_bkgndZBm = nullptr;
	if (ts.checkString("zbuffer")) {
		ts.scanString(" zbuffer %256s", 1, buf);
		// Don't even try to load if it's the "none" bitmap
		if (strcmp(buf, "<none>.lbm") != 0) {
			_bkgndZBm = Bitmap::create(buf);
			Debug::debug(Debug::Bitmaps | Debug::Sets,
						 "Loading scene z-buffer bitmap: %s\n", buf);
		}
	}

	ts.scanString(" position %f %f %f", 3, &_pos.x(), &_pos.y(), &_pos.z());
	ts.scanString(" interest %f %f %f", 3, &_interest.x(), &_interest.y(), &_interest.z());
	ts.scanString(" roll %f", 1, &_roll);
	ts.scanString(" fov %f", 1, &_fov);
	ts.scanString(" nclip %f", 1, &_nclip);
	ts.scanString(" fclip %f", 1, &_fclip);
	for (;;) {
		char name[256], zname[256];
		char bitmap[256], zbitmap[256];
		zbitmap[0] = '\0';
		if (ts.checkString("object_art"))
			ts.scanString(" object_art %256s %256s", 2, name, bitmap);
		else
			break;
		if (ts.checkString("object_z"))
			ts.scanString(" object_z %256s %256s", 2, zname, zbitmap);

		if (zbitmap[0] == '\0' || strcmp(name, zname) == 0) {
			set->addObjectState(id, ObjectState::OBJSTATE_BACKGROUND, bitmap, zbitmap, true);
		}
	}
}

void Set::Setup::loadBinary(Common::SeekableReadStream *data) {
	char name[128];
	data->read(name, 128);
	_name = Common::String(name);

	// Skip an unknown number (this is the stringlength of the following string)
	int fNameLen = 0;
	fNameLen = data->readUint32LE();

	char *fileName = new char[fNameLen];
	data->read(fileName, fNameLen);

	_bkgndZBm = nullptr;
	_bkgndBm = loadBackground(fileName);

	_pos.readFromStream(data);

	Math::Quaternion q;
	q.readFromStream(data);
	q.toMatrix(_rot);

	_fov   = data->readFloatLE();
	_nclip = data->readFloatLE();
	_fclip = data->readFloatLE();

	delete[] fileName;
}

void Set::Setup::saveState(SaveGame *savedState) const {
	//name
	savedState->writeString(_name);

	//bkgndBm
	if (_bkgndBm) {
		savedState->writeLESint32(_bkgndBm->getId());
	} else {
		savedState->writeLESint32(0);
	}

	// bkgndZBm
	if (_bkgndZBm) {
		savedState->writeLESint32(_bkgndZBm->getId());
	} else {
		savedState->writeLESint32(0);
	}

	savedState->writeVector3d(_pos);
	if (g_grim->getGameType() == GType_MONKEY4) {
		// Get the rotation matrix as a quaternion and write it out
		Math::Quaternion q(_rot);
		savedState->writeFloat(q.x());
		savedState->writeFloat(q.y());
		savedState->writeFloat(q.z());
		savedState->writeFloat(q.w());
	} else {
		savedState->writeVector3d(_interest);
		savedState->writeFloat(_roll);
	}
	savedState->writeFloat(_fov);
	savedState->writeFloat(_nclip);
	savedState->writeFloat(_fclip);
}

bool Set::Setup::restoreState(SaveGame *savedState) {
	_name = savedState->readString();

	_bkgndBm = Bitmap::getPool().getObject(savedState->readLESint32());
	_bkgndZBm = Bitmap::getPool().getObject(savedState->readLESint32());

	_pos      = savedState->readVector3d();
	if (g_grim->getGameType() == GType_MONKEY4) {
		float x = savedState->readFloat();
		float y = savedState->readFloat();
		float z = savedState->readFloat();
		float w = savedState->readFloat();
		Math::Quaternion q(x, y, z, w);
		_rot = q.toMatrix();
	} else {
		_interest = savedState->readVector3d();
		_roll     = savedState->readFloat();
	}
	_fov      = savedState->readFloat();
	_nclip    = savedState->readFloat();
	_fclip    = savedState->readFloat();

	return true;
}

Light::Light() : _falloffNear(0.0f), _falloffFar(0.0f), _enabled(false), _id(0) {
	setIntensity(0.0f);
	setUmbra(0.0f);
	setPenumbra(0.0f);
}

void Set::Setup::getRotation(float *x, float *y, float *z) {
	Math::Angle aX, aY, aZ;
	if (g_grim->getGameType() == GType_MONKEY4)
		_rot.getEuler(&aX, &aY, &aZ, Math::EO_ZYX);
	else
		_rot.getEuler(&aX, &aY, &aZ, Math::EO_ZXY);

	if (x != nullptr)
		*x = aX.getDegrees();
	if (y != nullptr)
		*y = aY.getDegrees();
	if (z != nullptr)
		*z = aZ.getDegrees();
}

void Set::Setup::setPitch(Math::Angle pitch) {
	Math::Angle oldYaw, oldRoll;
	if (g_grim->getGameType() == GType_MONKEY4) {
		_rot.getEuler(&oldRoll, &oldYaw, nullptr, Math::EO_ZYX);
		_rot.buildFromEuler(oldRoll, oldYaw, pitch, Math::EO_ZYX);
	} else {
		_rot.getEuler(&oldYaw, nullptr, &oldRoll, Math::EO_ZXY);
		_rot.buildFromEuler(oldYaw, pitch, oldRoll, Math::EO_ZXY);
	}
}

void Set::Setup::setYaw(Math::Angle yaw) {
	Math::Angle oldPitch, oldRoll;
	if (g_grim->getGameType() == GType_MONKEY4) {
		_rot.getEuler(&oldRoll, nullptr, &oldPitch, Math::EO_ZYX);
		_rot.buildFromEuler(oldRoll, yaw, oldPitch, Math::EO_ZYX);
	} else {
		_rot.getEuler(nullptr, &oldPitch, &oldRoll, Math::EO_ZXY);
		_rot.buildFromEuler(yaw, oldPitch, oldRoll, Math::EO_ZXY);
	}
}

void Set::Setup::setRoll(Math::Angle roll) {
	Math::Angle oldPitch, oldYaw;
	if (g_grim->getGameType() == GType_MONKEY4) {
		_rot.getEuler(nullptr, &oldYaw, &oldPitch, Math::EO_ZYX);
		_rot.buildFromEuler(roll, oldYaw, oldPitch, Math::EO_ZYX);
	} else {
		_rot.getEuler(&oldYaw, &oldPitch, nullptr, Math::EO_ZXY);
		_rot.buildFromEuler(oldYaw, oldPitch, roll, Math::EO_ZXY);
	}
}

void Light::setUmbra(float angle) {
	_umbraangle = angle;
	_cosumbraangle = cosf(angle * (float)M_PI / 180.0f);
}

void Light::setPenumbra(float angle) {
	_penumbraangle = angle;
	_cospenumbraangle = cosf(angle * (float)M_PI / 180.0f);
}

void Light::setIntensity(float intensity) {
	_intensity = intensity;
	if (g_grim->getGameType() == GType_MONKEY4) {
		_scaledintensity = intensity / 255;
	} else {
		_scaledintensity = intensity / 15;
	}
}

void Light::load(TextSplitter &ts) {
	char buf[256];
	float tmp;

	// Light names can be null, but ts doesn't seem flexible enough to allow this
	if (strlen(ts.getCurrentLine()) > strlen(" light"))
		ts.scanString(" light %256s", 1, buf);
	else {
		ts.nextLine();
		buf[0] = '\0';
	}
	_name = buf;

	ts.scanString(" type %256s", 1, buf);
	Common::String type = buf;
	if (type == "spot") {
		_type = Spot;
	} else if (type == "omni") {
		_type = Omni;
	} else if (type == "direct") {
		_type = Direct;
	} else {
		error("Light::load() Unknown type of light: %s", buf);
	}

	ts.scanString(" position %f %f %f", 3, &_pos.x(), &_pos.y(), &_pos.z());
	ts.scanString(" direction %f %f %f", 3, &_dir.x(), &_dir.y(), &_dir.z());
	ts.scanString(" intensity %f", 1, &tmp);
	setIntensity(tmp);
	ts.scanString(" umbraangle %f", 1, &tmp);
	setUmbra(tmp);
	ts.scanString(" penumbraangle %f", 1, &tmp);
	setPenumbra(tmp);

	int r, g, b;
	ts.scanString(" color %d %d %d", 3, &r, &g, &b);
	_color.getRed() = r;
	_color.getGreen() = g;
	_color.getBlue() = b;

	_enabled = true;
}

void Light::loadBinary(Common::SeekableReadStream *data) {
	char name[32];
	data->read(name, 32);
	_name = name;

	_pos.readFromStream(data);

	Math::Quaternion quat;
	quat.readFromStream(data);

	_dir.set(0, 0, -1);
	Math::Matrix4 rot = quat.toMatrix();
	rot.transform(&_dir, false);

	// This relies on the order of the LightType enum.
	_type = (LightType)data->readSint32LE();

	setIntensity(data->readFloatLE());

	int j = data->readSint32LE();
	// This always seems to be 0
	if (j != 0) {
		warning("Light::loadBinary j != 0");
	}

	_color.getRed() = data->readSint32LE();
	_color.getGreen() = data->readSint32LE();
	_color.getBlue() = data->readSint32LE();

	_falloffNear = data->readFloatLE();
	_falloffFar = data->readFloatLE();
	setUmbra(data->readFloatLE());
	setPenumbra(data->readFloatLE());

	_enabled = true;
}

void Light::saveState(SaveGame *savedState) const {
	// name
	savedState->writeString(_name);
	savedState->writeBool(_enabled);

	// type
	savedState->writeLEUint32(_type);

	savedState->writeVector3d(_pos);
	savedState->writeVector3d(_dir);

	savedState->writeColor(_color);

	savedState->writeFloat(_intensity);
	savedState->writeFloat(_umbraangle);
	savedState->writeFloat(_penumbraangle);

	savedState->writeFloat(_falloffNear);
	savedState->writeFloat(_falloffFar);
}

bool Light::restoreState(SaveGame *savedState) {
	_name = savedState->readString();
	_enabled = savedState->readBool();
	if (savedState->saveMinorVersion() > 7) {
		if (savedState->saveMinorVersion() >= 12) {
			_type = (LightType)savedState->readLEUint32();
		} else {
			int type = savedState->readLEUint32();
			if (type == 1) {
				_type = Spot;
			} else if (type == 2) {
				_type = Direct;
			} else if (type == 3) {
				_type = Omni;
			} else if (type == 4) {
				_type = Ambient;
			}
		}
	} else {
		Common::String type = savedState->readString();
		if (type == "spot") {
			_type = Spot;
		} else if (type == "omni") {
			_type = Omni;
		} else if (type == "direct") {
			_type = Direct;
		}
	}

	_pos           = savedState->readVector3d();
	_dir           = savedState->readVector3d();

	_color         = savedState->readColor();

	setIntensity(    savedState->readFloat());
	setUmbra(        savedState->readFloat());
	setPenumbra(     savedState->readFloat());

	if (savedState->saveMinorVersion() >= 20) {
		_falloffNear = savedState->readFloat();
		_falloffFar = savedState->readFloat();
	}

	return true;
}

SetShadow::SetShadow() : _numSectors(0) {
}

void SetShadow::loadBinary(Common::SeekableReadStream *data, Set *set) {
	uint32 nameLen = data->readUint32LE();
	char *name = new char[nameLen];
	data->read(name, nameLen);
	_name = Common::String(name);

	int lightNameLen = data->readSint32LE();
	char *lightName = new char[lightNameLen];
	data->read(lightName, lightNameLen);

	_shadowPoint.readFromStream(data);

	if (lightNameLen > 0) {
		for (Common::List<Light *>::const_iterator it = set->getLights(false).begin(); it != set->getLights(false).end(); ++it) {
			if ((*it)->_name.equals(lightName)) {
				_shadowPoint = (*it)->_pos;
				break;
			}
		}
	}

	int numSectors = data->readSint32LE();
	for (int i = 0; i < numSectors; ++i) {
		uint32 sectorNameLen = data->readUint32LE();
		char *sectorName = new char[sectorNameLen];
		data->read(sectorName, sectorNameLen);
		_sectorNames.push_back(sectorName);
		delete[] sectorName;
	}

	data->skip(4); // Unknown
	_color._vals[0] = (byte)data->readSint32LE();
	_color._vals[1] = (byte)data->readSint32LE();
	_color._vals[2] = (byte)data->readSint32LE();
	delete[] lightName;
	delete[] name;
}

void SetShadow::saveState(SaveGame *savedState) const {
	savedState->writeString(_name);
	savedState->writeVector3d(_shadowPoint);
	savedState->writeLESint32(_numSectors);
	savedState->writeLEUint32(_sectorNames.size());
	for (Common::List<Common::String>::const_iterator it = _sectorNames.begin(); it != _sectorNames.end(); ++it) {
		savedState->writeString(*it);
	}
	savedState->writeColor(_color);
}

void SetShadow::restoreState(SaveGame *savedState) {
	_name = savedState->readString();
	_shadowPoint = savedState->readVector3d();
	_numSectors = savedState->readLESint32();
	uint numSectors = savedState->readLEUint32();
	for (uint i = 0; i < numSectors; ++i) {
		_sectorNames.push_back(savedState->readString());
	}
	_color = savedState->readColor();
}

void Set::Setup::setupCamera() const {
	// Ignore nclip_ and fclip_ for now in Grim.  This fixes:
	// (a) Nothing was being displayed in the Land of the Living
	// diner because lr.set set nclip to 0.
	// (b) The zbuffers for setups with different nclip or
	// fclip values.  If it turns out that the clipping planes
	// are important at some point, we'll need to modify the
	// zbuffer transformation in bitmap.cpp to take nclip_ and
	// fclip_ into account.
	if (g_grim->getGameType() == GType_GRIM) {
		Math::Vector3d cameraPos = _pos;
		Math::Vector3d cameraInterest = _interest;
		const Math::Vector2d cameraPlaneShift = g_grim->getParallaxDebugCameraPlaneOffset();
		const Math::Vector3d parallaxOffset = g_grim->getParallaxDebugCameraOffset(_pos, _interest, _roll);
		const float screenPlaneDistance = MAX((_interest - _pos).getMagnitude(), 0.01f);
		cameraPos += parallaxOffset;
		cameraInterest += parallaxOffset;

		g_driver->setupCameraFrustum(_fov, 0.01f, 3276.8f, cameraPlaneShift, screenPlaneDistance);
		g_driver->positionCamera(cameraPos, cameraInterest, _roll);
	} else {
		g_driver->setupCameraFrustum(_fov, _nclip, _fclip);
		g_driver->positionCamera(_pos, _rot);
	}
}

class Sorter {
public:
	Sorter(const Math::Vector3d &pos) {
		_pos = pos;
	}

	bool operator()(Light *l1, Light *l2) const {
		float d1 = (l1->_pos - _pos).getSquareMagnitude();
		float d2 = (l2->_pos - _pos).getSquareMagnitude();
		if (d1 == d2) {
			return l1->_id < l2->_id;
		}

		return d1 < d2;
	}

	Math::Vector3d _pos;
};

void Set::setupLights(const Math::Vector3d &pos, bool inOverworld) {
	if (g_grim->getGameType() == GType_MONKEY4 && !g_driver->supportsShaders()) {
		// If shaders are not available, we do lighting in software for EMI.
		g_driver->disableLights();
		return;
	}

	if (!_enableLights) {
		g_driver->disableLights();
		return;
	}

	// Sort the ligths from the nearest to the farthest to the pos.
	Sorter sorter(pos);
	Common::List<Light *>* lightsList = inOverworld ? &_overworldLightsList : &_lightsList;
	Common::sort(lightsList->begin(), lightsList->end(), sorter);

	int count = 0;
	for (Light *l : *lightsList) {
		if (l->_enabled) {
			g_driver->setupLight(l, count);
			++count;
		}
	}
}

void Set::turnOffLights() {
	_enableLights = false;
	int count = 0;
	for (int i = 0; i < _numLights; i++) {
		Light *l = &_lights[i];
		if (l->_enabled) {
			g_driver->turnOffLight(count);
			++count;
		}
	}
}

void Set::setSetup(int num) {
	// Looks like num is zero-based so >= should work to find values
	// that are out of the range of valid setups

	// Quite weird, but this is what the original does when the setup id is above
	// the upper bound.
	if (num >= _numSetups)
		num %= _numSetups;

	if (num < 0) {
		error("Failed to change scene setup, value out of range");
		return;
	}
	_currSetup = _setups + num;
	g_grim->flagRefreshShadowMask(true);
	if (g_emiSound) {
		g_emiSound->updateSoundPositions();
	}
}

Bitmap::Ptr Set::loadBackground(const char *fileName) {
	Bitmap::Ptr bg = Bitmap::create(fileName);
	if (!bg) {
		Debug::warning(Debug::Bitmaps | Debug::Sets,
					   "Unable to load scene bitmap: %s, loading dfltroom instead", fileName);
		if (g_grim->getGameType() == GType_MONKEY4) {
			bg = Bitmap::create("dfltroom.til");
		} else {
			bg = Bitmap::create("dfltroom.bm");
		}
		if (!bg) {
			Debug::error(Debug::Bitmaps | Debug::Sets, "Unable to load dfltroom");
		}
	} else {
		Debug::debug(Debug::Bitmaps | Debug::Sets,
					 "Loaded scene bitmap: %s", fileName);
	}
	return bg;
}

void Set::drawBackground() const {
	const bool useLayeredBackground = g_grim->isParallaxDebugEnabled() &&
		g_grim->getGameType() == GType_GRIM &&
		g_grim->getParallaxDebugRenderMode() == GrimEngine::kParallaxDebugRenderLayered &&
		_currSetup &&
		_currSetup->_bkgndBm;
	const bool useDepthAwareBackground = g_grim->isParallaxDebugEnabled() &&
		g_grim->getGameType() == GType_GRIM &&
		g_grim->getParallaxDebugRenderMode() == GrimEngine::kParallaxDebugRenderWarp &&
		g_driver->supportsShaders() &&
		_currSetup->_bkgndBm &&
		_currSetup->_bkgndZBm;

	int offsetX = 0;
	int offsetY = 0;
	if (!useDepthAwareBackground && !useLayeredBackground)
		g_grim->getParallaxDebugScreenOffset(0.70f, offsetX, offsetY);

	if (_currSetup->_bkgndZBm) // Some screens have no zbuffer mask (eg, Alley)
		_currSetup->_bkgndZBm->draw(_currSetup->_bkgndZBm->getBitmapData()->_x + offsetX,
		                            _currSetup->_bkgndZBm->getBitmapData()->_y + offsetY);

	if (!_currSetup->_bkgndBm) {
		// This should fail softly, for some reason jumping to the signpost (sg) will load
		// the scene in such a way that the background isn't immediately available
		warning("Background hasn't loaded yet for setup %s in %s!", _currSetup->_name.c_str(), _name.c_str());
	} else {
		const int drawX = _currSetup->_bkgndBm->getBitmapData()->_x + offsetX;
		const int drawY = _currSetup->_bkgndBm->getBitmapData()->_y + offsetY;

		if (useLayeredBackground) {
			LayeredParallaxBackdrop *layered = loadLayeredParallaxBackdropForSetup(*_currSetup);
			if (layered && layered->baseBitmap) {
				int baseOffsetX = 0;
				int baseOffsetY = 0;
				g_grim->getParallaxDebugScreenOffset(layered->baseFactor, baseOffsetX, baseOffsetY);
				layered->baseBitmap->draw(layered->originX + baseOffsetX, layered->originY + baseOffsetY);

				Common::String layerSummary = Common::String::format("base@(%d:%d)f=%.4f", baseOffsetX, baseOffsetY, layered->baseFactor);

				for (uint i = 0; i < layered->layers.size(); ++i) {
					if (!layered->layers[i].bitmap)
						continue;
					int layerOffsetX = 0;
					int layerOffsetY = 0;
					g_grim->getParallaxDebugScreenOffset(layered->layers[i].factor, layerOffsetX, layerOffsetY);
					layered->layers[i].bitmap->draw(layered->originX + layerOffsetX, layered->originY + layerOffsetY);
					layerSummary += Common::String::format("|%s@(%d:%d)f=%.4f",
						layered->layers[i].name.c_str(), layerOffsetX, layerOffsetY, layered->layers[i].factor);
				}

				g_grim->updateParallaxDebugLayeredFrameState(_currSetup->_name, true, true, false,
					_currSetup->_parallaxBackdropLoadStatus.empty() ? "loaded" : _currSetup->_parallaxBackdropLoadStatus,
					layered->manifestPath, layered->baseFactor, baseOffsetX, baseOffsetY, layered->layers.size(), layerSummary);
				return;
			}

			g_grim->updateParallaxDebugLayeredFrameState(_currSetup->_name, true, false, true,
				_currSetup->_parallaxBackdropLoadStatus.empty() ? "missing" : _currSetup->_parallaxBackdropLoadStatus,
				_currSetup->_parallaxBackdropResolvedManifestPath, 0.0f, 0, 0, 0, Common::String("fallback_static_room_bitmap"));
			_currSetup->_bkgndBm->draw(drawX, drawY);
		} else if (useDepthAwareBackground) {
			g_grim->updateParallaxDebugLayeredFrameState(_currSetup->_name, false, false, false,
				"warp_mode", Common::String(), 0.0f, 0, 0, 0, Common::String());
			const Math::Vector2d cameraPlaneShift = g_grim->getParallaxDebugCameraPlaneOffset();
			const float screenPlaneDistance = MAX((_currSetup->_interest - _currSetup->_pos).getMagnitude(), 0.01f);
			g_driver->drawDepthAwareBackground(_currSetup->_bkgndBm, drawX, drawY, cameraPlaneShift, _currSetup->_fov, 0.01f, 3276.8f, screenPlaneDistance);
		} else {
			g_grim->updateParallaxDebugLayeredFrameState(_currSetup->_name, false, false, false,
				"static_mode", Common::String(), 0.0f, 0, 0, 0, Common::String());
			_currSetup->_bkgndBm->draw(drawX, drawY);
		}
	}
}

void Set::drawBitmaps(ObjectState::Position stage) {
	int offsetX = 0;
	int offsetY = 0;
	const bool useDepthAwareBackground = g_grim->isParallaxDebugEnabled() &&
		g_grim->getGameType() == GType_GRIM &&
		g_grim->getParallaxDebugRenderMode() == GrimEngine::kParallaxDebugRenderWarp &&
		g_driver->supportsShaders() &&
		_currSetup->_bkgndBm &&
		_currSetup->_bkgndZBm;

	// The main backdrop is already being depth-warped in the shader path. Applying
	// heuristic screen-space shifts to the auxiliary bitmap layers causes visible
	// seams where those layers no longer line up with the warped room image.
	if (!useDepthAwareBackground) {
		float parallaxFactor = 0.0f;
		switch (stage) {
		case ObjectState::OBJSTATE_BACKGROUND:
			parallaxFactor = 0.80f;
			break;
		case ObjectState::OBJSTATE_STATE:
			parallaxFactor = 0.90f;
			break;
		case ObjectState::OBJSTATE_UNDERLAY:
			parallaxFactor = 1.00f;
			break;
		case ObjectState::OBJSTATE_OVERLAY:
			parallaxFactor = 1.10f;
			break;
		}

		g_grim->getParallaxDebugScreenOffset(parallaxFactor, offsetX, offsetY);
	}

	for (StateList::iterator i = _states.reverse_begin(); i != _states.end(); --i) {
		if ((*i)->getPos() == stage && _currSetup == _setups + (*i)->getSetupID())
			(*i)->draw(offsetX, offsetY);
	}
}

void Set::setupCamera() {
	_currSetup->setupCamera();
	_frustum.setup(g_driver->getProjection() * g_driver->getModelView());
}

Sector *Set::findPointSector(const Math::Vector3d &p, Sector::SectorType type) {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if (sector && (sector->getType() & type) && sector->isVisible() && sector->isPointInSector(p))
			return sector;
	}
	return nullptr;
}

int Set::findSectorSortOrder(const Math::Vector3d &p, Sector::SectorType type) {
	int setup = getSetup();
	int sortOrder = 0;
	float minDist = 0.01f;

	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if (!sector || (sector->getType() & type) == 0 || !sector->isVisible() || setup >= sector->getNumSortplanes())
			continue;

		Math::Vector3d closestPt = sector->getClosestPoint(p);
		float thisDist = (closestPt - p).getMagnitude();
		if (thisDist < minDist) {
			minDist = thisDist;
			sortOrder = sector->getSortplane(setup);
		}
	}
	return sortOrder;
}

void Set::findClosestSector(const Math::Vector3d &p, Sector **sect, Math::Vector3d *closestPoint) {
	Sector *resultSect = nullptr;
	Math::Vector3d resultPt = p;
	float minDist = 0.0;

	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if ((sector->getType() & Sector::WalkType) == 0 || !sector->isVisible())
			continue;
		Math::Vector3d closestPt = sector->getClosestPoint(p);
		float thisDist = (closestPt - p).getMagnitude();
		if (!resultSect || thisDist < minDist) {
			resultSect = sector;
			resultPt = closestPt;
			minDist = thisDist;
		}
	}

	if (sect)
		*sect = resultSect;

	if (closestPoint)
		*closestPoint = resultPt;
}

void Set::shrinkBoxes(float radius) {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		sector->shrink(radius);
	}
}

void Set::unshrinkBoxes() {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		sector->unshrink();
	}
}

void Set::setLightIntensity(const char *light, float intensity) {
	for (int i = 0; i < _numLights; ++i) {
		Light &l = _lights[i];
		if (l._name == light) {
			l.setIntensity(intensity);
			return;
		}
	}
}

void Set::setLightIntensity(int light, float intensity) {
	Light &l = _lights[light];
	l.setIntensity(intensity);
}

void Set::setLightEnabled(const char *light, bool enabled) {
	for (int i = 0; i < _numLights; ++i) {
		Light &l = _lights[i];
		if (l._name == light) {
			l._enabled = enabled;
			return;
		}
	}
}

void Set::setLightEnabled(int light, bool enabled) {
	Light &l = _lights[light];
	l._enabled = enabled;
}

void Set::setLightPosition(const char *light, const Math::Vector3d &pos) {
	for (int i = 0; i < _numLights; ++i) {
		Light &l = _lights[i];
		if (l._name == light) {
			l._pos = pos;
			return;
		}
	}
}

void Set::setLightPosition(int light, const Math::Vector3d &pos) {
	Light &l = _lights[light];
	l._pos = pos;
}

void Set::setSoundPosition(const char *soundName, const Math::Vector3d &pos) {
	setSoundPosition(soundName, pos, _minVolume, _maxVolume);
}

void Set::setSoundPosition(const char *soundName, const Math::Vector3d &pos, int minVol, int maxVol) {
	int newBalance, newVolume;
	calculateSoundPosition(pos, minVol, maxVol, newVolume, newBalance);
	g_sound->setVolume(soundName, newVolume);
	g_sound->setPan(soundName, newBalance);
}

void Set::calculateSoundPosition(const Math::Vector3d &pos, int minVol, int maxVol, int &volume, int &balance) {
	// TODO: The volume and pan needs to be updated when the setup changes.
	// Note: This is only used in Grim. See SoundTrack::updatePosition for the corresponding implementation in EMI.

	// distance calculation
	Math::Vector3d cameraPos = _currSetup->_pos;
	Math::Vector3d vector = pos - cameraPos;
	float distance = vector.getMagnitude();
	float diffVolume = maxVol - minVol;
	// This 8.f is a guess, so it may need some adjusting
	int newVolume = (int)(8.f * diffVolume / distance);
	newVolume += minVol;
	if (newVolume > _maxVolume)
		newVolume = _maxVolume;
	volume = newVolume;
	float angle;

	Math::Vector3d cameraVector = _currSetup->_interest - _currSetup->_pos;
	Math::Vector3d up(0, 0, 1);
	Math::Vector3d right;
	cameraVector.normalize();
	float roll = -_currSetup->_roll * (float)M_PI / 180.f;
	float cosr = cos(roll);
	// Rotate the up vector by roll.
	up = up * cosr + Math::Vector3d::crossProduct(cameraVector, up) * sin(roll) +
	     cameraVector * Math::Vector3d::dotProduct(cameraVector, up) * (1 - cosr);
	right = Math::Vector3d::crossProduct(cameraVector, up);
	right.normalize();
	angle = atan2(Math::Vector3d::dotProduct(vector, right), Math::Vector3d::dotProduct(vector, cameraVector));

	float pan = sin(angle);
	balance = (int)((pan + 1.f) / 2.f * 127.f + 0.5f);
}

Sector *Set::getSectorBase(int id) {
	if ((_numSectors >= 0) && (id < _numSectors))
		return _sectors[id];
	else
		return nullptr;
}

Sector *Set::getSectorByName(const Common::String &name) {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if (sector->getName() == name) {
			return sector;
		}
	}
	return nullptr;
}

Sector *Set::getSectorBySubstring(const Common::String &str) {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if (strstr(sector->getName().c_str(), str.c_str())) {
			return sector;
		}
	}
	return nullptr;
}

Sector *Set::getSectorBySubstring(const Common::String &str, const Math::Vector3d &pos) {
	for (int i = 0; i < _numSectors; i++) {
		Sector *sector = _sectors[i];
		if (strstr(sector->getName().c_str(), str.c_str()) && sector->isPointInSector(pos)) {
			return sector;
		}
	}
	return nullptr;
}

void Set::setSoundParameters(int minVolume, int maxVolume) {
	_minVolume = minVolume;
	_maxVolume = maxVolume;
}

void Set::getSoundParameters(int *minVolume, int *maxVolume) {
	*minVolume = _minVolume;
	*maxVolume = _maxVolume;
}

void Set::addObjectState(const ObjectState::Ptr &s) {
	_states.push_front(s);
}

ObjectState *Set::addObjectState(int setupID, ObjectState::Position pos, const char *bitmap, const char *zbitmap, bool transparency) {
	ObjectState *state = findState(bitmap);

	if (state) {
		return state;
	}

	state = new ObjectState(setupID, pos, bitmap, zbitmap, transparency);
	addObjectState(state);

	return state;
}

ObjectState *Set::findState(const Common::String &filename) {
	// Check the different state objects for the bitmap
	for (StateList::iterator i = _states.begin(); i != _states.end(); ++i) {
		const Common::String &file = (*i)->getBitmapFilename();

		if (file == filename)
			return *i;
		if (file.compareToIgnoreCase(filename) == 0) {
			Debug::warning(Debug::Sets, "State object request '%s' matches object '%s' but is the wrong case", filename.c_str(), file.c_str());
			return *i;
		}
	}
	return nullptr;
}

void Set::moveObjectStateToFront(const ObjectState::Ptr &s) {
	_states.remove(s);
	_states.push_front(s);
	// Make the state invisible. This hides the deadbolt when brennis closes the switcher door
	// in the server room (tu), and therefore fixes https://github.com/residualvm/residualvm/issues/24
	s->setActiveImage(0);
}

void Set::moveObjectStateToBack(const ObjectState::Ptr &s) {
	_states.remove(s);
	_states.push_back(s);
}

SetShadow *Set::getShadow(int i) {
	return &_shadows[i];
}

SetShadow *Set::getShadowByName(const Common::String &name) {
	for (int i = 0; i < _numShadows; ++i) {
		SetShadow *shadow = &_shadows[i];
		if (shadow->_name.equalsIgnoreCase(name))
			return shadow;
	}
	return nullptr;
}

} // end of namespace Grim
