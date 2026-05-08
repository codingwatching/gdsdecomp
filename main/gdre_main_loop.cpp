#include "gdre_main_loop.h"
#include "scene/main/node.h"
#include "compat/resource_loader_compat.h"
GDREMainLoop *GDREMainLoop::singleton = nullptr;

GDREMainLoop::GDREMainLoop() {
	singleton = this;
}

GDREMainLoop::~GDREMainLoop() {
	singleton = nullptr;
}

GDREMainLoop *GDREMainLoop::get_singleton() {
	return singleton;
}

void GDREMainLoop::initialize() {
	ResourceCompatLoader::_init();
}

void GDREMainLoop::iteration_prepare() {
}

bool GDREMainLoop::physics_process(double p_time) {
	return false;
}

void GDREMainLoop::iteration_end() {
}

bool GDREMainLoop::process(double p_time) {
	return false;
}

void GDREMainLoop::finalize() {
}



// *** Scene Tree ***

void GDRESceneTree::initialize() {
	GDREMainLoop::get_singleton()->initialize();
	SceneTree::initialize();
}

void GDRESceneTree::iteration_prepare() {
	GDREMainLoop::get_singleton()->iteration_prepare();
	SceneTree::iteration_prepare();
}

bool GDRESceneTree::physics_process(double p_time) {
	if (GDREMainLoop::get_singleton()->physics_process(p_time)){
		return true;
	}
	return SceneTree::physics_process(p_time);
}

void GDRESceneTree::iteration_end() {
	GDREMainLoop::get_singleton()->iteration_end();
	SceneTree::iteration_end();
}

bool GDRESceneTree::process(double p_time) {
	if (GDREMainLoop::get_singleton()->process(p_time)){
		return true;
	}
	return SceneTree::process(p_time);
}

void GDRESceneTree::finalize() {
	GDREMainLoop::get_singleton()->finalize();
	SceneTree::finalize();
}

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"

class GDRENodeEndIteration : public Node {
	GDCLASS(GDRENodeEndIteration, Node);
public:
	void _notification(int p_what);
	GDRENodeEndIteration();
};


void _init_callback() {
	// the FRONT of the main node to ensure it runs at the start of the iteration
	EditorNode::get_singleton()->add_child(memnew(GDREMainLoopNode), false, Node::INTERNAL_MODE_FRONT);
	// the BACK of the main node to ensure it runs at the end of the iteration
	EditorNode::get_singleton()->add_child(memnew(GDRENodeEndIteration), false, Node::INTERNAL_MODE_BACK);
	GDREMainLoop::get_singleton()->initialize();

}

GDREMainLoopNode::GDREMainLoopNode() {
	set_process(true);
	set_physics_process(true);
	set_process_internal(true);
	set_physics_process_internal(true);
}

GDREMainLoopNode::~GDREMainLoopNode() {
}

void GDREMainLoopNode::setup() {
	ERR_FAIL_COND_MSG(!EditorNode::get_singleton(), "Make sure to call this during an init callback!");
	// if we're in the middle of the init callback, this ensure that we're called last.
	EditorNode::get_singleton()->add_init_callback(_init_callback);
}

void GDREMainLoopNode::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE:
			GDREMainLoop::get_singleton()->finalize();
			break;

		case NOTIFICATION_PROCESS:
			GDREMainLoop::get_singleton()->process(get_process_delta_time());
			break;

		case NOTIFICATION_PHYSICS_PROCESS:
			GDREMainLoop::get_singleton()->physics_process(get_physics_process_delta_time());
			break;

		case NOTIFICATION_INTERNAL_PROCESS:
			break;

		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS:
			GDREMainLoop::get_singleton()->iteration_prepare();
			break;
	}
}

void GDRENodeEndIteration::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PHYSICS_PROCESS:
			GDREMainLoop::get_singleton()->iteration_end();
			break;
	}
}

GDRENodeEndIteration::GDRENodeEndIteration() {
	set_physics_process(true);
}
#endif
