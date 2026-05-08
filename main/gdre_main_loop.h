#pragma once
#include "scene/main/scene_tree.h"
#include "scene/main/node.h"

// We have a SceneTree and a regular node class that call into our singleton because of the editor; normally we would be able to
// just use the SceneTree class, but when running in editor mode, Main::start() always sets the main loop to the base SceneTree class and
// that cannot be overridden.

class GDREMainLoop : public Object {
	GDCLASS(GDREMainLoop, Object);
	static GDREMainLoop *singleton;
public:
	static GDREMainLoop *get_singleton();
	void initialize();
	void iteration_prepare();
	bool physics_process(double p_time);
	void iteration_end();
	bool process(double p_time);
	void finalize();

	GDREMainLoop();
	~GDREMainLoop();
};

class GDRESceneTree : public SceneTree {
	GDCLASS(GDRESceneTree, SceneTree);
public:
	virtual void initialize() override;
	virtual void iteration_prepare() override;
	virtual bool physics_process(double p_time) override;
	virtual void iteration_end() override;
	virtual bool process(double p_time) override;
	virtual void finalize() override;
};

#ifdef TOOLS_ENABLED
class GDREMainLoopNode : public Node {
	GDCLASS(GDREMainLoopNode, Node);

protected:
	bool _init();
	void _notification(int p_what);
public:

	static void setup();

	GDREMainLoopNode();
	~GDREMainLoopNode();
};
#endif
