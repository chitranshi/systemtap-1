#include "GraphWidget.hxx"
#include "StapParser.hxx"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <map>

#include <signal.h>

#include <gtkmm.h>
#include <gtkmm/stock.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <gtkmm/scrolledwindow.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace systemtap;

class GrapherWindow : public Gtk::Window
{
public:
  GrapherWindow();
  virtual ~GrapherWindow() {}
  Gtk::VBox m_Box;
  Gtk::ScrolledWindow scrolled;
  GraphWidget w;
protected:
  virtual void on_menu_file_quit();
  void addGraph();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;

};

GrapherWindow::GrapherWindow()
{
  set_title("systemtap grapher");
  add(m_Box);


  //Create actions for menus and toolbars:
  m_refActionGroup = Gtk::ActionGroup::create();
  //File menu:
  m_refActionGroup->add(Gtk::Action::create("FileMenu", "File"));
  m_refActionGroup->add(Gtk::Action::create("AddGraph", "Add graph"),
                        sigc::mem_fun(*this, &GrapherWindow::addGraph));
  m_refActionGroup->add(Gtk::Action::create("FileQuit", Gtk::Stock::QUIT),
                        sigc::mem_fun(*this, &GrapherWindow::on_menu_file_quit));
  m_refUIManager = Gtk::UIManager::create();
  m_refUIManager->insert_action_group(m_refActionGroup);

  add_accel_group(m_refUIManager->get_accel_group());
  //Layout the actions in a menubar and toolbar:
  Glib::ustring ui_info =
    "<ui>"
    "  <menubar name='MenuBar'>"
    "    <menu action='FileMenu'>"
    "      <menuitem action='AddGraph'/>"    
    "      <menuitem action='FileQuit'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";
  try
    {
      m_refUIManager->add_ui_from_string(ui_info);
    }
  catch(const Glib::Error& ex)
    {
      std::cerr << "building menus failed: " <<  ex.what();
    }
  Gtk::Widget* pMenubar = m_refUIManager->get_widget("/MenuBar");
  scrolled.add(w);
  if(pMenubar)
    m_Box.pack_start(*pMenubar, Gtk::PACK_SHRINK);
  m_Box.pack_start(scrolled, Gtk::PACK_EXPAND_WIDGET);
  scrolled.show();

  show_all_children();

}
void GrapherWindow::on_menu_file_quit()
{
  hide();
}

// magic for noticing that the child stap process has died.
int childPid = -1;

int signalPipe[2] = {-1, -1};

extern "C"
{
  void handleChild(int signum, siginfo_t* info, void* context)
  {
    char buf[1];
    ssize_t err;
    buf[0] = 1;
    err = write(signalPipe[1], buf, 1);
  }
}

class SignalReader
{
public:
  SignalReader(GrapherWindow& win_, int sigfd_) : win(win_), sigfd(sigfd_) {}
  bool ioCallback(Glib::IOCondition ioCondition)
  {
    if ((ioCondition & Glib::IO_IN) == 0)
      return true;
    char buf;
    
    if (read(sigfd, &buf, 1) <= 0)
      return true;
    int status;
    while (wait(&status) != -1)
      ;
    childPid = -1;
    win.hide();
    return true;
  }
private:
  GrapherWindow& win;
  int sigfd;
};

int main(int argc, char** argv)
{
  Gtk::Main app(argc, argv);

  GrapherWindow win;

  win.set_title("Grapher");
  win.set_default_size(600, 200);

  StapParser stapParser(win, win.w);

  int stapErrFd = -1;
  if (argc > 1)
    {
      if (pipe(&signalPipe[0]) < 0)
        {
          std::perror("pipe");
          exit(1);
        }
      struct sigaction action;
      action.sa_sigaction = handleChild;
      sigemptyset(&action.sa_mask);
      action.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;
      sigaction(SIGCLD, &action, 0);
      int pipefd[4];
      if (pipe(&pipefd[0]) < 0)
        {
          std::perror("pipe");
          exit(1);
        }
      if (pipe(&pipefd[2]) < 0)
        {
          std::perror("pipe");
          exit(1);
        }
      if ((childPid = fork()) == -1)
        {
          exit(1);
        }
      else if (childPid)
        {
          dup2(pipefd[0], STDIN_FILENO);
          stapErrFd = pipefd[2];
          close(pipefd[0]);
          close(pipefd[1]);
          close(pipefd[3]);
        }
      else
        {
          dup2(pipefd[1], STDOUT_FILENO);
          dup2(pipefd[3], STDERR_FILENO);
          for (int i = 0; i < 4; ++i)
            close(pipefd[i]);
          char argv0[] = "stap";
          argv[0] = argv0;
          execvp("stap", argv);
          exit(1);
          return 1;
        }
     }
   Glib::signal_io().connect(sigc::mem_fun(stapParser,
                                           &StapParser::ioCallback),
                             STDIN_FILENO,
                             Glib::IO_IN | Glib::IO_HUP);
   if (stapErrFd >= 0)
     {
       stapParser.setErrFd(stapErrFd);
       Glib::signal_io().connect(sigc::mem_fun(stapParser,
                                               &StapParser::errIoCallback),
                                 stapErrFd,
                                 Glib::IO_IN);
     }
   SignalReader sigReader(win, signalPipe[0]);
   if (signalPipe[0] >= 0)
     {
       Glib::signal_io().connect(sigc::mem_fun(sigReader,
                                               &SignalReader::ioCallback),
                                 signalPipe[0], Glib::IO_IN);
     }
   Gtk::Main::run(win);
   if (childPid > 0)
   kill(childPid, SIGTERM);
   int status;
   while (wait(&status) != -1)
     ;
   return 0;
}

void GrapherWindow::addGraph()
{
  w.addGraph();
  
}
