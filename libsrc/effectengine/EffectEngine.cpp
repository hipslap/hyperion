// Python includes
#include <Python.h>

// Qt includes
#include <QMetaType>

// effect engine includes
#include <effectengine/EffectEngine.h>
#include "Effect.h"

EffectEngine::EffectEngine(Hyperion * hyperion, const Json::Value & jsonEffectConfig) :
	_hyperion(hyperion),
	_availableEffects(),
	_activeEffects(),
	_mainThreadState(nullptr)
{
	qRegisterMetaType<std::vector<ColorRgb>>("std::vector<ColorRgb>");

	// connect the Hyperion channel clear feedback
	connect(_hyperion, SIGNAL(channelCleared(int)), this, SLOT(channelCleared(int)));
	connect(_hyperion, SIGNAL(allChannelsCleared()), this, SLOT(allChannelsCleared()));

	// read all effects
	std::vector<std::string> effectNames = jsonEffectConfig.getMemberNames();
	for (const std::string & name : effectNames)
	{
		const Json::Value & info = jsonEffectConfig[name];
		_availableEffects[name] = {info["script"].asString(), info["args"]};
	}

	// initialize the python interpreter
	std::cout << "Initializing Python interpreter" << std::endl;
	Py_InitializeEx(0);
	PyEval_InitThreads(); // Create the GIL
	_mainThreadState = PyEval_SaveThread();
}

EffectEngine::~EffectEngine()
{
	// clean up the Python interpreter
	std::cout << "Cleaning up Python interpreter" << std::endl;
	PyEval_RestoreThread(_mainThreadState);
	Py_Finalize();
}

std::list<std::string> EffectEngine::getEffects() const
{
	std::list<std::string> effectNames;
	foreach (auto entry, _availableEffects) {
		effectNames.push_back(entry.first);
	}
	return effectNames;
}

int EffectEngine::runEffect(const std::string &effectName, int priority, int timeout)
{
	std::cout << "run effect " << effectName << " on channel " << priority << std::endl;

	if (_availableEffects.find(effectName) == _availableEffects.end())
	{
		// no such effect
		return -1;
	}

	// clear current effect on the channel
	channelCleared(priority);

	// create the effect
	const EffectDefinition & effectDefinition = _availableEffects[effectName];
	Effect * effect = new Effect(priority, timeout, effectDefinition.script, effectDefinition.args);
	connect(effect, SIGNAL(setColors(int,std::vector<ColorRgb>,int)), _hyperion, SLOT(setColors(int,std::vector<ColorRgb>,int)), Qt::QueuedConnection);
	connect(effect, SIGNAL(effectFinished(Effect*)), this, SLOT(effectFinished(Effect*)));
	_activeEffects.push_back(effect);

	// start the effect
	effect->start();

	return 0;
}

void EffectEngine::channelCleared(int priority)
{
	for (Effect * effect : _activeEffects)
	{
		if (effect->getPriority() == priority)
		{
			effect->abort();
		}
	}
}

void EffectEngine::allChannelsCleared()
{
	for (Effect * effect : _activeEffects)
	{
		effect->abort();
	}
}

void EffectEngine::effectFinished(Effect *effect)
{
	if (!effect->isAbortRequested())
	{
		// effect stopped by itself. Clear the channel
		_hyperion->clear(effect->getPriority());
	}

	std::cout << "effect finished" << std::endl;
	for (auto effectIt = _activeEffects.begin(); effectIt != _activeEffects.end(); ++effectIt)
	{
		if (*effectIt == effect)
		{
			_activeEffects.erase(effectIt);
			break;
		}
	}

	// cleanup the effect
	effect->deleteLater();
}
