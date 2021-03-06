/* -*- Mode: C++; tab-width: 8; c-basic-offset: 8; indent-tabs-mode: t; -*- */

#include "session.h"

#include "emufs.h"
#include "task.h"
#include "util.h"

using namespace std;

AddressSpace::shr_ptr
Session::create_vm(Task* t)
{
	AddressSpace::shr_ptr as(new AddressSpace(t, *this));
	as->insert_task(t);
	sas.insert(as.get());
	return as;
}

AddressSpace::shr_ptr
Session::clone(AddressSpace::shr_ptr vm)
{
	return AddressSpace::shr_ptr(new AddressSpace(*vm));
}

Task*
Session::clone(Task* p, int flags, void* stack, void* cleartid_addr,
	       pid_t new_tid, pid_t new_rec_tid)
{
	Task* c = p->clone(flags, stack, cleartid_addr, new_tid, new_rec_tid);
	track(c);
	return c;
}

TaskGroup::shr_ptr
Session::create_tg(Task* t)
{
	TaskGroup::shr_ptr tg(new TaskGroup(t->rec_tid, t->tid));
	tg->insert_task(t);
	return tg;
}

void
Session::dump_all_tasks(FILE* out)
{
	out = out ? out : stderr;

	for (auto as : sas) {
		auto ts = as->task_set();
		Task* t = *ts.begin();
		// XXX assuming that address space == task group,
		// which is almost certainly what the kernel enforces
		// too.
		fprintf(out, "\nTask group %d, image '%s':\n",
			t->tgid(), as->exe_image().c_str());
		for (auto tsit = ts.begin(); tsit != ts.end(); ++tsit) {
			(*tsit)->dump(out);
		}
	}
}

Task*
Session::find_task(pid_t rec_tid)
{
	auto it = tasks().find(rec_tid);
	return tasks().end() != it ? it->second : nullptr;
}

void
Session::kill_all_tasks()
{
	while (!task_map.empty()) {
		Task* t = task_map.rbegin()->second;
		t->kill();
		delete t;
	}
}

void
Session::on_destroy(AddressSpace* vm)
{
	assert(vm->task_set().size() == 0);
	sas.erase(vm);
}

void
Session::on_destroy(Task* t)
{
	task_map.erase(t->rec_tid);
	task_priority_set.erase(make_pair(t->priority, t));
}

void
Session::track(Task* t)
{
	task_map[t->rec_tid] = t;
	task_priority_set.insert(make_pair(t->priority, t));
}

void
Session::update_task_priority(Task* t, int value)
{
	task_priority_set.erase(make_pair(t->priority, t));
	t->priority = value;
	task_priority_set.insert(make_pair(t->priority, t));
}

Task*
RecordSession::create_task(const struct args_env& ae, shr_ptr self)
{
	assert(self.get() == this);
	Task* t = Task::spawn(ae, *this);
	track(t);
	t->session_record = self;
	return t;
}

/*static*/ RecordSession::shr_ptr
RecordSession::create(const string& exe_path)
{
	shr_ptr session(new RecordSession());
	session->trace_ofstream = TraceOfstream::create(exe_path);
	return session;
}

Task*
ReplaySession::create_task(const struct args_env& ae, shr_ptr self,
			   pid_t rec_tid)
{
	assert(self.get() == this);
	Task* t = Task::spawn(ae, *this, rec_tid);
	track(t);
	t->session_replay = self;
	return t;
}

void
ReplaySession::gc_emufs()
{
	emu_fs->gc(*this);
}

/*static*/ ReplaySession::shr_ptr
ReplaySession::create(int argc, char* argv[])
{
	shr_ptr session(new ReplaySession());
	session->emu_fs = EmuFs::create();
	session->trace_ifstream = TraceIfstream::open(argc, argv);
	return session;
}

void
ReplaySession::restart()
{
	kill_all_tasks();
	assert(tasks().size() == 0 && vms().size() == 0);
	last_debugged_task = nullptr;
	tgid_debugged = 0;
	tracees_consistent = false;

	gc_emufs();
	assert(emufs().size() == 0);

	trace_ifstream->rewind();
}
