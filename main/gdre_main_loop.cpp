#include "gdre_main_loop.h"

#include "compat/resource_loader_compat.h"
#include "core/object/object.h"
#include "core/os/os.h"
#include "main/main.h"
#include "scene/main/node.h"
#include "servers/rendering/rendering_server.h"
#include "utility/gdre_settings.h"
#include "utility/task_manager.h"

GDREMainLoop *GDREMainLoop::singleton = nullptr;
bool GDREMainLoop::testing = false;

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
	last_physics_process_time = p_time;
	return false;
}

void GDREMainLoop::iteration_end() {
}

bool GDREMainLoop::process(double p_time) {
	last_process_time = p_time;
	_wait_until_next_frame(p_time * 1000000 / 2, true);
	return false;
}

void GDREMainLoop::finalize() {
}

bool GDREMainLoop::wait_until_next_frame(int64_t p_time_usec) {
	if (!singleton) {
		GDREMainLoop::iteration(false);
	}
	return singleton->_wait_until_next_frame(p_time_usec, false);
}

bool GDREMainLoop::_wait_until_next_frame(int64_t input_time_usec, bool called_from_process) {
	int64_t p_time_usec = MAX(1, input_time_usec);
	uint64_t curr_time = OS::get_singleton()->get_ticks_usec();
	if (!Thread::is_main_thread()) {
		OS::get_singleton()->delay_usec(p_time_usec);
		return TaskManager::get_singleton()->is_current_task_canceled();
	}
	if (processing) {
		return false;
	}
	processing = true;
	TaskManager::get_singleton()->process_main_thread_dispatch_queue_for(p_time_usec);
	bool did_redraw = false;
	if (TaskManager::get_singleton()->update_progress_bg(!called_from_process, called_from_process, &did_redraw)) {
		TaskManager::get_singleton()->cancel_main_thread_dispatch_queue();
		processing = false;
		return true;
	}
	if (!called_from_process) {
		iteration(true);
	}
	int64_t elapsed_time = OS::get_singleton()->get_ticks_usec() - curr_time;
	constexpr int64_t SYNC_WAIT_TIME_US = 1000;
	if (elapsed_time < p_time_usec) {
		while (elapsed_time < p_time_usec) {
			RS::get_singleton()->sync();
			elapsed_time = OS::get_singleton()->get_ticks_usec() - curr_time;
			if (p_time_usec - elapsed_time > SYNC_WAIT_TIME_US) {
				OS::get_singleton()->delay_usec(SYNC_WAIT_TIME_US);
			} else {
				if (p_time_usec - elapsed_time > 0) {
					OS::get_singleton()->delay_usec(p_time_usec - elapsed_time);
				}
				break;
			}
		}
	}
	processing = false;
	return false;
}

#ifdef TESTS_ENABLED
void GDREMainLoop::set_is_testing(bool p_is_testing) {
	testing = p_is_testing;
}

bool GDREMainLoop::is_testing() {
	return testing;
}
#endif

bool GDREMainLoop::iteration(bool p_no_delay) {
	// For testing, we can't call Main::iteration() because Main hasn't been set up.
	// We only attempt to sync the renderingserver to flush the messages queue during testing.
#ifdef TESTS_ENABLED
	if (testing) {
		if (RenderingServer::get_singleton()) {
			RenderingServer::get_singleton()->sync();
			if (MessageQueue::get_singleton() && !MessageQueue::get_singleton()->is_flushing()) {
				MessageQueue::get_singleton()->flush();
			}
		}
		return false;
	}
#endif

	int64_t time_delay = Engine::get_singleton()->get_frame_delay();
	if (p_no_delay) {
		Engine::get_singleton()->set_frame_delay(0);
	}
	bool result = Main::iteration();
	if (p_no_delay) {
		Engine::get_singleton()->set_frame_delay(time_delay);
	}
	return result;
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
