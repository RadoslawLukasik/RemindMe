#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <audio/wave.h>
#include <sys/stat.h>
#include <errno.h>
#include <xapp/libxapp/xapp-status-icon.h>

#define FILEOPT "/options.dat"
#define FILEEVENT "/event.dat"
#define FILESOUND "/usr/share/RemindMe/alarm.wav"
#define FILEPNG "/usr/share/pixmaps/RemindMe.png"

typedef struct options{
    gint bSound;
    gint lang;
    gint startmini;
    gint dontclose;
    gint oldremindersonlyonce;
    gint reserved3;
    gint reserved2;
    gint reserved1;
} options;

typedef union tval{
    guint64 value;
    struct{
        guint8 min;
        guint8 hour;
        guint8 day;
        guint8 mon;
        guint32 year;
    };
} tval;

typedef struct events {
    union {
        guint64 value;
        struct{
            guint8 min;
            guint8 hour;
            guint8 day;
            guint8 mon;
            guint32 year;
        };
    };
    unsigned long long originalvalue;// the value of the postponed event
    guint16 repeatnum;// how many times repeat events (0 - infinity)
    guint16 repeatperiod;
    guint16 daysbefore;// dissplays notifications x days before the date of the event
    guint8 repeatperiodtype;
    guint8 postponed;
    gchar text[88];
    struct events *prev;
    struct events *next;
} events;

XAppStatusIcon *status;
GtkWidget *list;
GtkWidget *window,*wndopt,*wndedit, *wndevent;
GtkTreeSelection *selection;
GtkWidget *event_message, *event_year, *event_month, *event_day, *event_hour, *event_min, *event_period, *event_period_type, *event_repeat_num, *event_notification_before, *event_find;
GtkWidget *opt_sound, *opt_lang, *opt_startmini, *opt_dontclose, *opt_showoldremonce;
GtkWidget *postponed_combo, *postponed_spin;
GtkListStore *store;
GtkTreeIter  iterselected;

events *eventslist=NULL, eventnew, eventtm, *thev;
options opt = {1, 0, 0, 1};

gint NeedSave = 0;// ms to save list to file
gint NumEvent = 0;// number of events in list
gint ModType = 0; // 1 - adding new event, 2 - editing event
gint ShowEvent = 0;// 0 - show, 1 - don't show event now
gint bSoundLoaded = 0;// 1 - sound loaded
gint bHidden = 0;
gint bWindowWasShowed = 0;
atomic_int bChangesInList = 0;

gint listselected = 0;
time_t today = 0;
// global variables for the sound
ALCdevice *device;
ALCcontext *context;
ALuint buffer, source;
ALCenum error;

char pathopt[PATH_MAX];
char patheve[PATH_MAX];

enum {
enLang=0,
enListHeadDate,
enListHeadEvent,
enBtnAdd,
enBtnDel,
enBtnEdit,
enEntryFind,
enBtnExit,
enBtnOptions,
enBtnCancel,
enLblMessage,
enLblDateTime,
enLblDay,
enLblMonth,
enLblYear,
enLblHour,
enCBminutes,
enCBhours,
enCBdays,
enCBmonths,
enCByears,
enLblRepetitionsNum,
enLblRepeatPeriod,
enLblNotificationsBefore,
enCheckSound,
enLblLang,
enCheckStartMinimized,
enCheckDontClose,
enShowOldRemOnce,
enPostpone,
msgAskDel,
msgDirErr,
msgOptFileLoadErr,
msgOptFileSaveErr,
msgOptFileReadErr,
msgOptFileWriteErr,
msgEventFileLoadErr,
msgEventFileSaveErr,
msgEventFileReadErr,
msgEventFileWriteErr,
msgSoundFileLoadErr,
msgAllocErr,
msgBadMonthDay,
msgPastDate,
msgBadSel
};

#define nLanguages 2
gchar szLangStr[nLanguages][45][48] = {
/* english */   {   "english", "Date", "Event", "Add", "Delete", "Edit", "Message to find", "Exit", "Options", "Cancel", "Message",
                    "Date and time:", "Day", "Month", "Year", "Hour", "minutes", "hours","days", "months", "years",
                    "Number of repetitions (0 = infinite)", "Repeat period", "Show notifications before (days)",
                    "Play sounds", "Language (need restart)", "Start minimized", "Minimize instead of close",
                    "Show old reminders only once", "Postpone the event",
                    "Are you sure you would delete this entry?", "Cannot create a folder $HOME/.RemindMe",
                    "Cannot load options.dat", "Cannot save to options.dat", "Reading error from options.dat", "Writing error to options.dat",
                    "Cannot load event.dat", "Cannot save to event.dat", "Reading error from event.dat", "Writing error to event.dat",
                    "Cannot load alarm.wav", "Memory allocation error", "Incorrect day of the month", "The date must be a date in the future",
                    "Incorrect selected item in list"
                },


/* polski */    {   "polski", "Data", "Wydarzenie" ,"Dodaj", "Usuń", "Edytuj", "Komunikat do wyszukania", "Zakończ", "Ustawienia", "Anuluj", "Komunikat",
                    "Data i czas:", "Dzień", "Miesiąc", "Rok", "Godzina", "minut", "godzin", "dni", "miesięcy", "lat",
                    "liczba powtórzeń (0 = nieskończona)", "Okres powtarzania", "Pokaż powiadomienia przed  (dni)",
                    "Odtwarzaj dźwięki", "Język (wymaga ponownego uruchomienia)", "Uruchamiaj zminimalizowany", "Mnimalizacja zamiast zamykania",
                    "Pokaż stare przypomnienia tylko raz", "Przełóż wydarzenie o",
                    "Czy na pewno chcesz usunąć ten wpis?", "Nie można utworzyć katalogu $HOME/.RemindMe",
                    "Nie można odczytać z options.dat", "Nie można zapisać do options.dat", "Nie można odczytać z options.dat", "Nie można zapisać do options.dat",
                    "Nie można odczytać z event.dat", "Nie można zapisać do event.dat", "Nie można odczytać z event.dat", "Nie można zapisać do event.dat",
                    "Nie można załadować alarm.wav", "Błąd alokacji pamięci", "Nieprawidłowy dzień miesiąca", "Data musi być datą w przyszłości",
                    "Nieprawidłowe zaznaczenie w liście"
                }
};

// returns the number of days in the month m of the year y
guint8 days_in_month(guint8 m, guint y){
    guint8 dim;// dim = the number of days in the selected month
    switch(m){
    case 2:
        if(((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0))
            dim = 29;
        else
            dim = 28;
        break;
    case 4:
    case 6:
    case 9:
    case 11:
        dim = 30;
        break;
    default:
        dim = 31;
    }
    return dim;
}

// show the dialog window when some error
void show_error(GtkWidget *wnd, char *errmsg){
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(wnd),GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,"%s",errmsg);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

// show notifications
void show_notification(gchar * text){
    NotifyNotification * Hello = notify_notification_new ("RemindMe", text, "dialog-information");
    notify_notification_show (Hello, NULL);
    g_object_unref(G_OBJECT(Hello));
}

static inline ALenum to_al_format(short channels, short samples){
	char stereo = (channels > 1);
	switch (samples) {
	case 16:
		if (stereo)
			return AL_FORMAT_STEREO16;
		else
			return AL_FORMAT_MONO16;
	case 8:
		if (stereo)
			return AL_FORMAT_STEREO8;
		else
			return AL_FORMAT_MONO8;
	default:
		return -1;
	}
}

#define TEST_ERROR(_msg)		\
	error = alGetError();		\
	if (error != AL_NO_ERROR) {	\
		show_notification(_msg );	\
		return -1;		\
	}
// initialize the sound
gint sound_init(){
	const ALCchar *defaultDeviceName;
	int ret;
	WaveInfo *wave;
	char *bufferData;
	ALfloat listenerOri[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };

    defaultDeviceName = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
	device = alcOpenDevice(defaultDeviceName);
	if (!device) {
		show_notification("unable to open default sound device\n");
		return -1;
	}
	alGetError();

	context = alcCreateContext(device, NULL);
	if (!alcMakeContextCurrent(context)) {
		show_notification("failed to make default sound context");
		return -1;
	}
	TEST_ERROR("make default sound context error");

	/* set orientation */
	alListener3f(AL_POSITION, 0, 0, 1.0f);
	TEST_ERROR("sound listener position error");
    	alListener3f(AL_VELOCITY, 0, 0, 0);
	TEST_ERROR("sound listener velocity error");
	alListenerfv(AL_ORIENTATION, listenerOri);
	TEST_ERROR("sound listener orientation error");

	alGenSources((ALuint)1, &source);
	TEST_ERROR("sound source generation error");

	alSourcef(source, AL_PITCH, 1);
	TEST_ERROR("sound source pitch error");
	alSourcef(source, AL_GAIN, 1);
	TEST_ERROR("sound source gain error");
	alSource3f(source, AL_POSITION, 0, 0, 0);
	TEST_ERROR("sound source position error");
	alSource3f(source, AL_VELOCITY, 0, 0, 0);
	TEST_ERROR("sound source velocity error");
	alSourcei(source, AL_LOOPING, AL_TRUE);
	TEST_ERROR("sound source looping error");

	alGenBuffers(1, &buffer);
	TEST_ERROR("sound buffer generation error");
	wave = WaveOpenFileForReading(FILESOUND);
	if (!wave) {
		show_notification(szLangStr[opt.lang][msgSoundFileLoadErr]);
		return -1;
	}

	ret = WaveSeekFile(0, wave);
	if (ret) {
		show_notification("failed to seek sound file\n");
		return -1;
	}

	bufferData = malloc(wave->dataSize);
	if (!bufferData) {
		show_notification("sound malloc error");
		return -1;
	}

	ret = WaveReadFile(bufferData, wave->dataSize, wave);
	if (ret != wave->dataSize) {
		show_notification("sound short read");
		return -1;
	}

	alBufferData(buffer, to_al_format(wave->channels, wave->bitsPerSample), bufferData, wave->dataSize, wave->sampleRate);
	TEST_ERROR("failed to load sound buffer data");
	alSourcei(source, AL_BUFFER, buffer);// bind the source with its buffer.
	TEST_ERROR("sound buffer binding error");
	return 0;
}

void sound_uninit(){
    alDeleteSources(1, &source);
	alDeleteBuffers(1, &buffer);
	device = alcGetContextsDevice(context);
	alcMakeContextCurrent(NULL);
	alcDestroyContext(context);
	alcCloseDevice(device);
}

// delete the item from the list
void delete_item(){
    events *tmp;
    gint j = 0;
    tmp=eventslist;
    while(j<listselected){
        tmp=tmp->next;
        j++;
    }

    bChangesInList = 1;

    if(tmp->prev)
        (tmp->prev)->next = tmp->next;
    if(tmp->next)
        (tmp->next)->prev = tmp->prev;
    if(listselected == 0) // delete the first element of the list?
        eventslist = eventslist-> next;
    NumEvent--;
    NeedSave = 60000;

    bChangesInList = 0;

    free(tmp);
    gtk_list_store_remove(store, &iterselected);
}

// user clicked remove
void remove_item(GtkWidget *widget, gpointer selection) {
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkWidget *dialog;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(selection), &model, &iterselected)) {
        path = gtk_tree_model_get_path (model, &iterselected);
        gint *i = gtk_tree_path_get_indices(path);
        listselected = i[0];
        if(listselected < NumEvent){// propably we don't need it
            dialog = gtk_message_dialog_new (GTK_WINDOW (window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_YES_NO, "%s", szLangStr[opt.lang][0]);
            gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
            if(gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES)
                delete_item();
            gtk_widget_destroy (dialog);
        }
    }
}

// adding/editing events
gboolean add_eventnew(){
    GtkTreeIter tri;
    events *enew, *tmp;
    gint pos=0;
    gchar data[]="RRRR/MM/DD";
    enew = (events*) malloc(sizeof(events));
    if(enew){
        if(ModType == 2)// if we edit, then first we delete the old event
            delete_item();
        *enew = eventnew;
        enew->prev = 0;
        enew->next = eventslist;
        for(tmp=eventslist; tmp !=0; tmp = tmp->next){
            if(tmp->value > enew->value) break;
            pos++;
            enew->prev = tmp;
            enew->next = tmp->next;
        }

        bChangesInList = 1;

        if(enew->prev){
            (enew->prev)->next = enew;
        } else {
            eventslist = enew;
        }
        if(enew->next){
            (enew->next)->prev = enew;
        }
        NumEvent++;
        NeedSave = 60000;

        bChangesInList = 0;

        data[0]=(eventnew.year/1000 % 10)+'0';
        data[1]=(eventnew.year/100 % 10)+'0';
        data[2]=(eventnew.year/10 % 10)+'0';
        data[3]=(eventnew.year % 10)+'0';
        data[5]=(eventnew.mon/10 % 10)+'0';
        data[6]=(eventnew.mon % 10)+'0';
        guint dim = days_in_month(eventnew.mon,eventnew.year);
        if(dim > eventnew.day)
            dim = eventnew.day;
        data[8]=(dim/10 % 10)+'0';
        data[9]=(dim % 10)+'0';
        gtk_list_store_insert(store,&tri,pos);
        gtk_list_store_set(store,&tri,0,data,1,enew->text,-1);
        return FALSE;
    } else {
        show_error(wndedit,szLangStr[opt.lang][msgAllocErr]);
        return TRUE;
    }
}

// initialization of gtk_tree_view
void init_list(GtkWidget *list) {
    GtkCellRenderer    *renderer;
    GtkTreeViewColumn  *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(szLangStr[opt.lang][enListHeadDate], renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);
    column = gtk_tree_view_column_new_with_attributes(szLangStr[opt.lang][enListHeadEvent], renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(list), column);

    store = gtk_list_store_new(2, G_TYPE_STRING,G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(list), GTK_TREE_MODEL(store));
    //g_object_unref(store);
}

// user clicked Cancel in the add/edit event window
void edit_cancel(GtkWidget *widget, gpointer data) {
    gtk_widget_destroy(wndedit);
    gtk_widget_set_sensitive(window,TRUE);
    ModType = 0;
}

// user clicked Ok in the add/edit event window
void edit_ok(GtkWidget *widget, gpointer data) {
    eventnew.year = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_year));
    eventnew.mon = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_month));
    eventnew.day = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_day));
    eventnew.hour = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_hour));
    eventnew.min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_min));
    if(days_in_month(eventnew.mon,eventnew.year) < eventnew.day){// the incorect day in the month?
        show_error(wndedit,szLangStr[opt.lang][msgBadMonthDay]);
    } else {
        time_t acttime = time(NULL);
        struct tm *datetime = localtime(&acttime);
        tval val;
        val.year = datetime->tm_year+1900;
        val.mon = datetime->tm_mon+1;
        val.day = datetime->tm_mday;
        val.hour = datetime->tm_hour;
        val.min = datetime->tm_min;
        if(val.value >= eventnew.value){// the date is in the past?
            show_error(wndedit,szLangStr[opt.lang][msgPastDate]);
        } else {
            eventnew.repeatperiod = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_period));
            eventnew.repeatperiodtype = gtk_combo_box_get_active(GTK_COMBO_BOX(event_period_type));
            eventnew.repeatnum = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_repeat_num));
            eventnew.daysbefore = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(event_notification_before));
            eventnew.postponed = 0;
            eventnew.originalvalue = 0;
            strncpy(eventnew.text,gtk_entry_get_text(GTK_ENTRY(event_message)),87);
            add_eventnew();
            edit_cancel(widget, data);
        }
    }
}

// the add/edit event window was closed
gboolean edit_delete_event (GtkWidget *widget, GdkEvent  *event, gpointer   data){
    gtk_widget_set_sensitive(window,TRUE);
    ModType = 0;
    return FALSE;
}

// create add/edit event windows
void init_edit_wnd(void){
    // boxes, buttons, label, separator
    GtkWidget *vbox, *vbox2, *hbox, *hbox2;
    GtkWidget *btnok, *btncancel;
    GtkWidget *label;
    GtkWidget *separator;

    wndedit = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(wndedit),GTK_WINDOW(window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(wndedit),TRUE);
    gtk_window_set_position(GTK_WINDOW(wndedit), GTK_WIN_POS_CENTER);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(wndedit),TRUE);
    gtk_container_set_border_width(GTK_CONTAINER (wndedit), 8);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // message
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblMessage]);
    event_message = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(event_message),eventnew.text);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), event_message, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // date and time
    label = gtk_label_new(szLangStr[opt.lang][enLblDateTime]);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 2);
    // date
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblYear]);
    event_year = gtk_spin_button_new_with_range(eventnew.year,eventnew.year+100,1);// range of year
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_year),eventnew.year);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_year),TRUE);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox2), event_year, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox2, FALSE, TRUE, 2);
    hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblMonth]);
    event_month = gtk_spin_button_new_with_range(1,12,1);// range of month
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_month),eventnew.mon);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_month),TRUE);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox2), event_month, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox2, FALSE, TRUE, 2);
    hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblDay]);
    event_day = gtk_spin_button_new_with_range(1,31,1);// range of day*
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_day),eventnew.day);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_day),TRUE);
    gtk_box_pack_start(GTK_BOX(hbox2), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox2), event_day, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox2, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox2, FALSE, TRUE, 2);
    // hour
    label = gtk_label_new(":");
    event_min = gtk_spin_button_new_with_range(0,59,1);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(event_min),GTK_ORIENTATION_VERTICAL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_min),eventnew.min);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_min),TRUE);
    gtk_box_pack_end(GTK_BOX(hbox), event_min, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    label = gtk_label_new(szLangStr[opt.lang][enLblHour]);
    event_hour = gtk_spin_button_new_with_range(0,23,1);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(event_hour),GTK_ORIENTATION_VERTICAL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_hour),eventnew.hour);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_hour),TRUE);
    gtk_box_pack_end(GTK_BOX(hbox), event_hour, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // repeat options
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblRepetitionsNum]);
    event_repeat_num = gtk_spin_button_new_with_range(0,65535,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_repeat_num),eventnew.repeatnum);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_repeat_num),TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), event_repeat_num, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // repeat period
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblRepeatPeriod]);
    event_period = gtk_spin_button_new_with_range(1,65535,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_period),eventnew.repeatperiod);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_period),TRUE);
    event_period_type = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_period_type),szLangStr[opt.lang][enCByears]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_period_type),szLangStr[opt.lang][enCBmonths]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_period_type),szLangStr[opt.lang][enCBdays]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_period_type),szLangStr[opt.lang][enCBhours]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event_period_type),szLangStr[opt.lang][enCBminutes]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(event_period_type),eventnew.repeatperiodtype);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), event_period_type, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), event_period, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // notifications before
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enLblNotificationsBefore]);
    event_notification_before = gtk_spin_button_new_with_range(0,65535,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(event_notification_before),eventnew.daysbefore);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(event_notification_before),TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), event_notification_before, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // buttons
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    btnok = gtk_button_new_with_label("Ok");
    btncancel = gtk_button_new_with_label(szLangStr[opt.lang][enBtnCancel]);

    gtk_box_pack_start(GTK_BOX(hbox), btnok, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), btncancel, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // ------
    gtk_container_add(GTK_CONTAINER(wndedit), vbox);
    // signals
    g_signal_connect(G_OBJECT(btnok), "clicked", G_CALLBACK(edit_ok), NULL);
    g_signal_connect(G_OBJECT(btncancel), "clicked", G_CALLBACK(edit_cancel), NULL);
    g_signal_connect (G_OBJECT(wndedit), "delete_event", G_CALLBACK (edit_delete_event), NULL);

    gtk_widget_show_all(wndedit);
}

// editing selected item in the list
void edit_item(GtkWidget *widget, gpointer selection) {
    GtkTreeModel *model;
    GtkTreePath *path;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    if (gtk_tree_selection_get_selected(GTK_TREE_SELECTION(selection), &model, &iterselected)) {
            // editing
        ModType = 2;
        path = gtk_tree_model_get_path (model, &iterselected);
        gint *i = gtk_tree_path_get_indices(path);
        listselected = i[0];
        if(listselected < NumEvent){// propably we don't need it
            gint j=0;
            events *tmp=eventslist;
            while(j<listselected){
                tmp=tmp->next;
                j++;
            }
            time_t acttime = time(NULL);
            struct tm *datetime = localtime(&acttime);
            eventnew = *tmp;
            if(eventnew.postponed > 0)
                eventnew.value = eventnew.originalvalue;
            eventnew.year = datetime->tm_year+1900;
            if((eventnew.mon <= datetime->tm_mon) || ((eventnew.mon-1 == datetime->tm_mon) && (eventnew.day < datetime->tm_mday)))
                eventnew.year++;
            init_edit_wnd();
            gtk_widget_set_sensitive(window,FALSE);
        } else {
            show_error(window,szLangStr[opt.lang][msgBadSel]);
        }
    }
}

// adding new item in the list
void add_item(GtkWidget *widget, gpointer selection) {
    time_t acttime = time(NULL);
    struct tm *datetime = localtime(&acttime);
    eventnew.day = datetime->tm_mday;
    eventnew.mon = datetime->tm_mon+1;
    eventnew.year = datetime->tm_year+1900;
    eventnew.hour = 0;
    eventnew.min = 0;
    eventnew.repeatnum = 0;
    eventnew.repeatperiod = 1;
    eventnew.repeatperiodtype = 0;
    eventnew.daysbefore = 0;
    eventnew.postponed = 0;
    eventnew.text[0] = 0;
    ModType = 1;
    init_edit_wnd();
    gtk_widget_set_sensitive(window,FALSE);
}

// functions for changing options
void opt_cancel(GtkWidget *widget, gpointer data) {
    gtk_widget_destroy(wndopt);
    gtk_widget_set_sensitive(window,TRUE);
}

// user clicked Ok in the options window
void opt_ok(GtkWidget *widget, gpointer data) {
    FILE *plik;
    opt.lang = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_lang));
    opt.bSound = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opt_sound));
    opt.startmini = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opt_startmini));
    opt.dontclose = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opt_dontclose));
    opt.oldremindersonlyonce = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(opt_showoldremonce));
    plik = fopen(pathopt,"wb");
    if(plik){
        // saving options
        if(fwrite(&opt,sizeof(opt),1,plik) != 1){
            show_error(wndopt,szLangStr[opt.lang][msgOptFileWriteErr]);
        }
        fclose(plik);
    } else {
        show_error(wndopt,szLangStr[opt.lang][msgOptFileSaveErr]);
    }
    opt_cancel(widget, data);
}

// user clicked the close button on the the titlebar of the options window
gboolean opt_delete_event (GtkWidget *widget, GdkEvent  *event, gpointer   data){
    gtk_widget_set_sensitive(window,TRUE);
    return FALSE;
}

// initialization of the options window
void init_opt_wnd(void){
    // boxes, buttons, label, combo
    GtkWidget *vbox, *hbox;
    GtkWidget *btnok, *btncancel;
    GtkWidget *label;
    GtkWidget *separator;

    wndopt = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_transient_for(GTK_WINDOW(wndopt),GTK_WINDOW(window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(wndopt),TRUE);
    gtk_window_set_resizable(GTK_WINDOW(wndopt), FALSE);
    gtk_window_set_position(GTK_WINDOW(wndopt), GTK_WIN_POS_CENTER);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(wndopt),TRUE);
    gtk_container_set_border_width(GTK_CONTAINER (wndopt), 8);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // sound option
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    opt_sound = gtk_check_button_new_with_label(szLangStr[opt.lang][enCheckSound]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opt_sound),opt.bSound);
    opt_startmini = gtk_check_button_new_with_label(szLangStr[opt.lang][enCheckStartMinimized]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opt_startmini),opt.startmini);
    opt_dontclose = gtk_check_button_new_with_label(szLangStr[opt.lang][enCheckDontClose]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opt_dontclose),opt.dontclose);
    opt_showoldremonce = gtk_check_button_new_with_label(szLangStr[opt.lang][enShowOldRemOnce]);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(opt_showoldremonce),opt.oldremindersonlyonce);
    gtk_box_pack_start(GTK_BOX(vbox), opt_sound, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), opt_startmini, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), opt_dontclose, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), opt_showoldremonce, TRUE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // language option
    label = gtk_label_new(szLangStr[opt.lang][enLblLang]);
    opt_lang = gtk_combo_box_text_new();
    for(int i=0;i<nLanguages;i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(opt_lang),szLangStr[i][0]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(opt_lang),opt.lang);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), opt_lang, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // buttons
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    btnok = gtk_button_new_with_label("Ok");
    btncancel = gtk_button_new_with_label(szLangStr[opt.lang][enBtnCancel]);

    gtk_box_pack_start(GTK_BOX(hbox), btnok, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), btncancel, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    gtk_container_add(GTK_CONTAINER(wndopt), vbox);

    g_signal_connect(G_OBJECT(btnok), "clicked", G_CALLBACK(opt_ok), NULL);
    g_signal_connect(G_OBJECT(btncancel), "clicked", G_CALLBACK(opt_cancel), NULL);
    g_signal_connect (G_OBJECT(wndopt), "delete_event", G_CALLBACK (opt_delete_event), NULL);

    gtk_widget_show_all(wndopt);
}

// adding time tval
void add_timetval(tval *dest, gint y, gint mon, gint d, gint h, gint min, gboolean bMoreDaysInMonthWhenAddMonths){
    gint8 dim;
    if(min > 0){
        min += dest->min;
        h += min / 60;
        dest->min = (min % 60);
    }
    if(h > 0){
        h += dest->hour;
        d += (h / 24);
        dest->hour = (h % 24);
    }
    if(d > 0){
        d += dest->day;
        while(1){
            dim=days_in_month(dest->mon,dest->year);
            if(dim >= d) break;
            d -= dim;
            dest->mon ++;
            if(dest->mon > 12){
                dest->mon -= 12;
                dest->year ++;
            }
        }
        dest->day = d;
    }
    if(mon > 0){
        mon += dest->mon;// if we adding only months (and years), then the day of month can be greater than it is possible, which means that it is the last day of the month
        while(mon > 12){
            mon -= 12;
            dest->year ++;
        }
        dest->mon = mon;
    }
    if(y > 0){
        dest->year += y;
    }
    if(!bMoreDaysInMonthWhenAddMonths){
        dim=days_in_month(dest->mon,dest->year);
        if(dim < dest->day){
            dest->day -= dim;
            dest->mon ++;// the month is <12, so we don't change the year
        }
    }
}
// user clicked Ok in the event window
void show_ok(GtkWidget *widget, gpointer data) {
    gint t = gtk_combo_box_get_active(GTK_COMBO_BOX(postponed_combo));
    gint n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(postponed_spin));
    eventnew = *eventslist;
    listselected = 0;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    gtk_tree_model_get_iter_first(model, &iterselected);
    time_t postponedtime = time(NULL);
    struct tm *datetime = localtime(&postponedtime);
    if(n){// we must postpone this event
        if(eventnew.postponed == 0){// event wasn't postponed earlier
            eventnew.originalvalue = eventnew.value;
            eventnew.postponed = 1;
        }
        eventnew.year = datetime->tm_year+1900;
        eventnew.mon = datetime->tm_mon+1;
        eventnew.day = datetime->tm_mday;
        eventnew.hour = datetime->tm_hour;
        eventnew.min = datetime->tm_min;
        switch(t){
        case 0:// adding minutes
            add_timetval((tval*) &(eventnew.value),0,0,0,0,n,FALSE);
            break;
        case 1:// adding hours
            add_timetval((tval*) &(eventnew.value),0,0,0,n,0,FALSE);
            break;
        case 2:// adding days
            add_timetval((tval*) &(eventnew.value),0,0,n,0,0,FALSE);
            eventnew.hour = 0;// we set hour:min to 00:00
            eventnew.min = 0;
        }
        ModType = 2;
        add_eventnew();
    } else {// we don't postpone this event
        if(eventnew.postponed > 0){
            eventnew.value = eventnew.originalvalue;
            eventnew.originalvalue = 0;
            eventnew.postponed = 0;
        }
        if(eventnew.repeatnum == 1){// we don't repeat this event
            delete_item();
        } else {// we repeat this event
            if(eventnew.repeatnum > 1)
                eventnew.repeatnum--;
            tval tv;
            tv.year = datetime->tm_year+1900;
            tv.mon = datetime->tm_mon+1;
            tv.day = datetime->tm_mday;
            tv.hour = datetime->tm_hour;
            tv.min = datetime->tm_min;
            switch(eventnew.repeatperiodtype){
            case 4:// minutes
                do{
                add_timetval((tval*) &(eventnew.value),0,0,0,0,eventnew.repeatperiod,FALSE);
                }while((opt.oldremindersonlyonce != 0) && (eventnew.value < tv.value));
                break;
            case 3:// hours
                do{
                add_timetval((tval*) &(eventnew.value),0,0,0,eventnew.repeatperiod,0,FALSE);
                }while((opt.oldremindersonlyonce != 0) && (eventnew.value < tv.value));
                break;
            case 2:// days
                do{
                add_timetval((tval*) &(eventnew.value),0,0,eventnew.repeatperiod,0,0,FALSE);
                }while((opt.oldremindersonlyonce != 0) && (eventnew.value < tv.value));
                break;
            case 1:// months
                do{
                add_timetval((tval*) &(eventnew.value),0,eventnew.repeatperiod,0,0,0,TRUE);
                }while((opt.oldremindersonlyonce != 0) && (eventnew.value < tv.value));
                break;
            case 0:// years
                do{
                add_timetval((tval*) &(eventnew.value),eventnew.repeatperiod,0,0,0,0,TRUE);
                }while((opt.oldremindersonlyonce != 0) && (eventnew.value < tv.value));
            }
            ModType = 2;
            add_eventnew();
        }
    }
    gtk_widget_destroy(wndevent);
    gtk_widget_set_sensitive(window,TRUE);
    ShowEvent = 0;
    ModType = 0;
    if(bSoundLoaded){// stop the sound
        alSourceStop(source);
        sound_uninit();
        bSoundLoaded = 0;
    }
}
// function called on exit
gboolean show_delete_event (GtkWidget *widget, GdkEvent  *event, gpointer   data){
    gtk_widget_set_sensitive(window,TRUE);
    ShowEvent = 0;
    if(bSoundLoaded){
        alSourceStop(source);
        sound_uninit();
        bSoundLoaded = 0;
    }
    return FALSE;
}

// create add/edit event windows
void init_event_wnd(void){
    // boxes, buttons, label, separator
    GtkWidget *vbox, *hbox;
    GtkWidget *btnok;
    GtkWidget *label;
    GtkWidget *separator;

    wndevent = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_keep_above(GTK_WINDOW(wndevent),TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(wndevent),GTK_WINDOW(window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(wndevent),TRUE);
    gtk_window_set_resizable(GTK_WINDOW(wndevent), FALSE);
    gtk_window_set_position(GTK_WINDOW(wndevent), GTK_WIN_POS_CENTER);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(wndevent),TRUE);
    gtk_container_set_border_width(GTK_CONTAINER (wndevent), 8);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // message
    label = gtk_label_new(eventslist->text);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // postponement options
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    label = gtk_label_new(szLangStr[opt.lang][enPostpone]);
    postponed_spin = gtk_spin_button_new_with_range(0,65535,1);// range of minutes/hours/days
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(postponed_spin),0);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(postponed_spin),TRUE);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(postponed_spin),GTK_ORIENTATION_HORIZONTAL);
    postponed_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(postponed_combo),szLangStr[opt.lang][enCBminutes]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(postponed_combo),szLangStr[opt.lang][enCBhours]);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(postponed_combo),szLangStr[opt.lang][enCBdays]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(postponed_combo),1);// default hours
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), postponed_combo, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), postponed_spin, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // separator
    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), separator, TRUE, TRUE, 2);
    // buttons
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    btnok = gtk_button_new_with_label("Ok");
    gtk_box_pack_start(GTK_BOX(vbox), btnok, FALSE, TRUE, 2);
    // ------
    gtk_container_add(GTK_CONTAINER(wndevent), vbox);
    // signals
    g_signal_connect(G_OBJECT(btnok), "clicked", G_CALLBACK(show_ok), NULL);
    g_signal_connect (G_OBJECT(wndevent), "delete_event", G_CALLBACK (show_delete_event), NULL);

    gtk_widget_show_all(wndevent);

}

// user clicked Options in the main window
void change_options(GtkWidget *widget, gpointer selection) {
    init_opt_wnd();
    gtk_widget_set_sensitive(window,FALSE);
}

// loading the options file
void opt_load() {
    const char *home = getenv("HOME");
    char save_dir[PATH_MAX];
    if(home){
        strcpy(save_dir,home);
    } else {// problem with $HOME
        strcpy(save_dir,"/tmp");
    }
    strcat(save_dir,"/.RemindMe");
    strcpy(pathopt,save_dir);
    strcpy(patheve,save_dir);
    strcat(pathopt,FILEOPT);
    strcat(patheve,FILEEVENT);
    FILE *plik = fopen(pathopt,"rb");
    if(plik){
        // load options
        if(fread(&opt,sizeof(opt),1,plik) != 1){
            show_error(window,szLangStr[opt.lang][msgOptFileReadErr]);
        }
        fclose(plik);
    } else {
        if(mkdir(save_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1){
            show_error(window,szLangStr[opt.lang][msgDirErr]);
        } else {
            plik = fopen(pathopt,"wb");
            if(plik){
                // saving options
                if(fwrite(&opt,sizeof(opt),1,plik) != 1){
                    show_error(window,szLangStr[opt.lang][msgOptFileWriteErr]);
                }
                fclose(plik);
            } else {
                show_error(window,szLangStr[opt.lang][msgOptFileSaveErr]);
            }
        }
    }
}

// loading events from the file
void datbase_load() {
    FILE *plik = fopen(patheve,"rb");
    if(plik){
        // load the database
        while(!feof(plik)){
            if(fread(&eventnew,sizeof(events),1,plik)==1){
                if(add_eventnew()) break;
            } else {
                if(!feof(plik)) show_error(window,szLangStr[opt.lang][msgEventFileReadErr]);
                break;
            }
        }
        NeedSave = 0;
        fclose(plik);
    } else {
        if(errno != ENOENT)
            show_error(window,szLangStr[opt.lang][msgEventFileLoadErr]);
    }
}

// saving events to the file
void datbase_save() {
    FILE *plik = fopen(patheve,"wb");
    if(plik){
        // save the database
        events *tmp = eventslist;
        while(tmp){
            if(fwrite(tmp,sizeof(events),1,plik)!=1){
                show_error(window,szLangStr[opt.lang][msgEventFileWriteErr]);
                NeedSave = 60000;// try save later
                break;
            }
            tmp=tmp->next;
        }
        fclose(plik);
    } else {
        show_error(window,szLangStr[opt.lang][msgEventFileSaveErr]);
        NeedSave = 60000;// try save later
    }
}

// function called on exit
gboolean on_delete_event (GtkWidget *widget, GdkEvent  *event, gpointer   data){
  /* If you return FALSE in the "delete_event" signal handler, GTK will emit the "destroy" signal.
   * Returning TRUE means you don't want the window to be destroyed.
   * This is useful for popping up 'are you sure you want to quit?' type dialogs.
   */
    if(NeedSave >0){
        datbase_save();
        NeedSave = 0;
    }
    if(opt.dontclose){// minimize only?
        gtk_widget_hide(window);
        bHidden = 1;
        return TRUE;
    } else{
        notify_uninit();
        g_object_unref(status);
        return FALSE;
    }
}

// cyclic check for upcoming events
gboolean time_handler(GtkWidget *widget) {
    if (window == NULL) return FALSE;
    static gchar text[100]="RRRR/MM/DD: ";
    static struct tm datetime;
    tval val,valev,valtmp;
    time_t acttime = time(NULL);
    datetime = *localtime(&acttime);
    val.year = datetime.tm_year+1900;
    val.mon = datetime.tm_mon+1;
    val.day = datetime.tm_mday;
    val.hour = datetime.tm_hour;
    val.min = datetime.tm_min;
    // if the program is running and we have new day, then we must check if we need to show notifications for events
    if(acttime - 86400 >= today){
        datetime.tm_hour = 0;
        datetime.tm_min = 0;
        datetime.tm_sec = 0;
        today = mktime(&datetime);
        thev = eventslist;
    }
    while(thev){// show notifications for events which need them
        if(bChangesInList == 0){
            if(thev->daysbefore > 0){
                valev.value = thev->value;
                guint dim = days_in_month(valev.mon,valev.year);
                valev.hour = 0;
                valev.min = 0;
                if(valev.day > dim)
                    valev.day = dim;
                valtmp = val;
                add_timetval(&valtmp,0,0,thev->daysbefore,0,0,FALSE);
                if(valev.value <= valtmp.value){// show notification
                    text[0]=(valev.year/1000 % 10)+'0';
                    text[1]=(valev.year/100 % 10)+'0';
                    text[2]=(valev.year/10 % 10)+'0';
                    text[3]=(valev.year % 10)+'0';
                    text[5]=(valev.mon/10 % 10)+'0';
                    text[6]=(valev.mon % 10)+'0';
                    text[8]=(valev.day/10 % 10)+'0';
                    text[9]=(valev.day % 10)+'0';
                    memcpy(&(text[12]),thev->text,88);
                    show_notification(text);
                    thev = thev->next;
                    break;
                }
            }
            thev = thev->next;
        } else
            break;
    }
    // show the window for actual events
    if((bChangesInList == 0) && eventslist){// if some event is displayed or we adding/editing some event, then we don't display the next event window
        valev.value = eventslist->value;
        guint dim = days_in_month(valev.mon,valev.year);
        if(valev.day > dim)
            valev.day = dim;
        if(valev.value <= val.value){
            if((ShowEvent == 0) && (ModType == 0)){
                ShowEvent = 1;
                init_event_wnd();
                if(opt.bSound){// play sound
                    if(sound_init() == 0){
                        bSoundLoaded = 1;
                        alSourcePlay(source);
                        TEST_ERROR("source playing error");
                    } else
                        bSoundLoaded = 0;
                }
            }
        }
    }
    // saving event.dat if we had some changes
    if((NeedSave > 0) && (bChangesInList == 0)){
        NeedSave -= 2000;
        if(NeedSave <= 0)
            datbase_save();
    }
    return TRUE;
}

// user clicked Exit in the main window
void wnd_exit(GtkWidget *widget, gpointer selection) {
    opt.dontclose = 0;
    gtk_window_close(GTK_WINDOW(window));
}

// our searching in list function
gboolean tree_view_search_equal_func(GtkTreeModel* model, gint column, const gchar* key, GtkTreeIter* iter, gpointer search_data){
    const gchar *str;
    gboolean     retval = TRUE;
    GValue       transformed = { 0, };
    GValue       value = { 0, };
    gchar       *case_normalized_string = NULL;
    gchar       *case_normalized_key = NULL;
    gchar       *normalized_string;
    gchar       *normalized_key;

    /* determine the value for the column/iter */
    gtk_tree_model_get_value (model, iter, column, &value);

    /* try to transform the value to a string */
    g_value_init (&transformed, G_TYPE_STRING);
    if (!g_value_transform (&value, &transformed)){
        g_value_unset (&value);
        return TRUE;
    }
    g_value_unset (&value);

    /* check if we have a string value */
    str = g_value_get_string (&transformed);
    if (G_UNLIKELY (str == NULL)){
        g_value_unset (&transformed);
        return TRUE;
    }

    /* normalize the string and the key */
    normalized_string = g_utf8_normalize (str, -1, G_NORMALIZE_ALL);
    normalized_key = g_utf8_normalize (key, -1, G_NORMALIZE_ALL);

    /* check if we have normalized both string */
    if (G_LIKELY (normalized_string != NULL && normalized_key != NULL)){
        case_normalized_string = g_utf8_casefold (normalized_string, -1);
        case_normalized_key = g_utf8_casefold (normalized_key, -1);
        /* compare the casefolded strings */
        if (strstr (case_normalized_string, case_normalized_key) != 0)
            retval = FALSE;
    }

    /* cleanup */
    g_free (case_normalized_string);
    g_free (case_normalized_key);
    g_value_unset (&transformed);
    g_free (normalized_string);
    g_free (normalized_key);

    return retval;
}

// show/hide the main window when user click on the status icon
void status_icon_press(XAppStatusIcon *icon, gint x, gint y, guint button, guint time, gint panel_position, gpointer user_data){
    if(bHidden != 0){
        if(bWindowWasShowed)
            gtk_widget_show(window);
        else
            gtk_widget_show_all(window);
        bHidden = 0;
    }else{
        gtk_widget_hide(window);
        bHidden = 1;
    }
}

// create the main window
void init_main_wnd(){
    // boxes, buttons
    GtkWidget *sw, *vbox, *hbox;
    GtkWidget *remove, *add, *edit, *options, *btnexit;

    // create the main window and the vertical box
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "RemindMe");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER (window), 8);
    gtk_widget_set_size_request(window, 720, 400);
    gtk_window_set_type_hint(GTK_WINDOW(window),GDK_WINDOW_TYPE_HINT_MENU);// remove minimize and maximize buttons in the titlebar

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    // create buttons in the horizontal box
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    add = gtk_button_new_with_label(szLangStr[opt.lang][enBtnAdd]);
    remove = gtk_button_new_with_label(szLangStr[opt.lang][enBtnDel]);
    edit = gtk_button_new_with_label(szLangStr[opt.lang][enBtnEdit]);
    options = gtk_button_new_with_label(szLangStr[opt.lang][enBtnOptions]);
    btnexit = gtk_button_new_with_label(szLangStr[opt.lang][enBtnExit]);

    event_find = gtk_entry_new();
    gtk_widget_set_size_request(event_find, 120, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(event_find),szLangStr[opt.lang][enEntryFind]);

    gtk_box_pack_start(GTK_BOX(hbox), add, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), edit, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), remove, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), event_find, TRUE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), btnexit, FALSE, TRUE, 2);
    gtk_box_pack_end(GTK_BOX(hbox), options, FALSE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 2);
    // create the list of events with scrollbar
    sw = gtk_scrolled_window_new(NULL, NULL);
    list = gtk_tree_view_new();
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(list),GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_container_add(GTK_CONTAINER(sw), list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    init_list(list);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
    gtk_tree_selection_set_mode(selection,GTK_SELECTION_SINGLE);// only one item can be selected
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(list),1);
    gtk_tree_view_set_search_entry(GTK_TREE_VIEW(list),GTK_ENTRY(event_find));
    gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(list), tree_view_search_equal_func, 0, 0);

    g_signal_connect(G_OBJECT(add), "clicked", G_CALLBACK(add_item), NULL);
    g_signal_connect(G_OBJECT(remove), "clicked", G_CALLBACK(remove_item), selection);
    g_signal_connect(G_OBJECT(edit), "clicked", G_CALLBACK(edit_item), selection);
    g_signal_connect(G_OBJECT(options), "clicked", G_CALLBACK(change_options), NULL);
    g_signal_connect(G_OBJECT(btnexit), "clicked", G_CALLBACK(wnd_exit), NULL);
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect (G_OBJECT(window), "delete_event", G_CALLBACK (on_delete_event), NULL);

    if(opt.startmini){// don't show the main window when the option startmini is set
        bHidden = 1;
    } else {
        gtk_widget_show_all(window);
    }

    // add the status icon in the system tray
    status = xapp_status_icon_new();
    xapp_status_icon_set_tooltip_text(status,"RemindMe");
    xapp_status_icon_set_icon_name(status,FILEPNG);
    g_signal_connect (status, "button-press-event",(GCallback) status_icon_press,0);

}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    opt_load();
    init_main_wnd();
    notify_init ("RemindMe");
    datbase_load();
    g_timeout_add_seconds(2,(GSourceFunc)time_handler,(gpointer)window);// create timer
    gtk_main();
    return 0;
}
