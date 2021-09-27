#include <mpd/client.h>
#include <stdio.h>
#include <mpd/connection.h>
#include <pulse/pulseaudio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>

//MPD variables
static struct mpd_connection* conn;

//Pulse Audio variables
static pa_threaded_mainloop* mainloop;
static pa_mainloop_api* api;
static pa_context* context;

//callbacks and other functions
void setup_pulse_audio();
void context_state_cb(pa_context* cntxt, void* mainloop);
void sink_input_info_cb(pa_context* cntxt, const pa_sink_input_info* info, int eol, void* userdata);
void setup_mpd();

void check_sink_inputs();
void actual_daemon_code();


//variables
static bool doneChecking;
static bool isAnythingElsePlaying;

int noOfSelectedPrograms;
char** selected_programs; // array of strings

int main(int argc, char** argv)
{

	noOfSelectedPrograms = argc - 1; // excluding the program name
	selected_programs = argv; 

	//daemonizing code start
	pid_t pid, sid;

	pid = fork(); 
	if(pid < 0)
	{
		exit(EXIT_FAILURE);
	}

	if(pid > 0)
	{
		printf("%d\n", pid);

		exit(EXIT_SUCCESS);
	}

	umask(0);

	sid = setsid();
	if(sid < 0)
	{
		exit(EXIT_FAILURE);
	}

	
	if((chdir("/")) < 0)
	{
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	//daemonizing code end	

	actual_daemon_code();

	exit(EXIT_SUCCESS);
}

void actual_daemon_code()
{
	doneChecking = false;
	isAnythingElsePlaying = false;

	setup_mpd();

	setup_pulse_audio();

	check_sink_inputs();

	//DO NOT USE while loop IT GIVES SEGMENTATION FAULT! IDK WHY
	for(;;)
	{
		check_sink_inputs();	

		if(doneChecking)
		{
			if(isAnythingElsePlaying)
			{
				mpd_run_pause(conn, true);
				
				//reset values
				isAnythingElsePlaying = false;
				doneChecking = false;
			}
			else
			{
				mpd_run_play(conn);
				
				//reset values
				isAnythingElsePlaying = false;
				doneChecking = false;
			}
		}
	}

	pa_threaded_mainloop_stop(mainloop);
	pa_threaded_mainloop_free(mainloop);

	mpd_connection_free(conn);
}

void check_sink_inputs()
{
	pa_operation* o;

	pa_threaded_mainloop_lock(mainloop);

	o = pa_context_get_sink_input_info_list(context, &sink_input_info_cb, NULL);
	assert(o);

	while(pa_operation_get_state(o) == PA_OPERATION_RUNNING)
	{
		pa_threaded_mainloop_wait(mainloop);
	}

	pa_operation_unref(o);


	pa_threaded_mainloop_unlock(mainloop);
}

void setup_mpd()
{
	conn = mpd_connection_new(NULL, 0, 0);
	assert(conn);

	if(mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS)
	{
		//fprintf(stderr, mpd_connection_get_error_message(conn));
		mpd_connection_free(conn);
		return;
	}

	
	struct mpd_status* status;
	status = mpd_status_begin();

	if(status == NULL)
	{
		//fprintf(stderr, mpd_status_get_error(status));
	}



	mpd_run_play(conn);
}

void setup_pulse_audio()
{
	mainloop = pa_threaded_mainloop_new();
	assert(mainloop);

	api = pa_threaded_mainloop_get_api(mainloop);

	context = pa_context_new(api, NULL);
	assert(context);


	pa_context_set_state_callback(context, &context_state_cb, mainloop);

	pa_threaded_mainloop_lock(mainloop);


	assert(pa_threaded_mainloop_start(mainloop) == 0);
	assert(pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) == 0);

	//infinite loop which waits till context is ready
	//DO NOT USE while loop IT CAUSES SEGMENTATION FAULT! IDK WHY
	for(;;)
	{
		pa_context_state_t context_state = pa_context_get_state(context);
		assert(PA_CONTEXT_IS_GOOD(context_state));

		if(context_state == PA_CONTEXT_READY)
			break;

		pa_threaded_mainloop_wait(mainloop);
	}

	pa_threaded_mainloop_unlock(mainloop);
}

void context_state_cb(pa_context* cntxt, void* mainloop)
{
	pa_threaded_mainloop_signal(mainloop, 0);
}

void sink_input_info_cb(pa_context* cntxt, const pa_sink_input_info* info, int eol, void* userdata)
{

	if(eol != 0) // if negative then error, if positive then end of list
	{
		doneChecking = true;

		pa_threaded_mainloop_signal(mainloop, 0);
		
		return;
	}
	
	const char* app_name = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	
	for(int i=0;i<noOfSelectedPrograms;i++)
	{
		isAnythingElsePlaying = isAnythingElsePlaying || (strcmp(app_name, selected_programs[i]) == 0);
	}

	pa_threaded_mainloop_signal(mainloop, 0);
}
