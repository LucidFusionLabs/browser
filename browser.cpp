/*
 * $Id: browser.cpp 1336 2014-12-08 09:29:59Z justin $
 * Copyright (C) 2009 Lucid Fusion Labs

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "core/app/app.h"
#include "core/app/gl/view.h"
#include "core/app/ipc.h"
#include "core/app/shell.h"
#include "core/web/browser/browser.h"
#include "core/web/browser/document.h"

namespace LFL {
DEFINE_string(url, "http://news.google.com/", "Url to open");
DEFINE_int(width, 1040, "browser width");
DEFINE_int(height, 768, "browser height");
DEFINE_bool(render_log, false, "Output render log");
DEFINE_bool(render_sandbox, true, "Render in sandbox process");
extern FlagOfType<bool> FLAGS_lfapp_network_;
Application *app;

struct JavaScriptConsole : public Console {
  Browser *browser;
  JavaScriptConsole(Window *W, Browser *B) : Console(W), browser(B)
  { bottom_or_top = 1; write_timestamp = 0; color = Color(Color::black, 0.9); }

  virtual void Run(const string &in) override {
    if (app->render_process) app->render_process->ExecuteScript(in, bind(&JavaScriptConsole::ExecuteResponseCB, this, _1));
    else { string ret = browser->doc.js_context->Execute(in); if (!ret.empty()) Write(ret); }
  }
  void ExecuteResponseCB(const string &res) { if (!res.empty()) Write(res); }
};

struct MyBrowserWindow : public View {
  Font *menu_atlas;
  Box win, topbar, addressbar;
  Widget::Button back, forward, refresh;
  TextBox address_box;
  unique_ptr<Browser> lfl_browser;
  unique_ptr<BrowserInterface> qt_browser, berkelium_browser;
  BrowserInterface *browser=0;
  BrowserController *controller=0;
  Browser::RenderLog render_log;

  MyBrowserWindow(Window *W) : View(W, "MyBrowserView"),
    menu_atlas(app->fonts->Get(W->gl_h, "MenuAtlas", "", 0, Color::white, Color::clear, 0)),
    back   (this, &menu_atlas->FindGlyph(6)->tex, "", MouseController::CB([&](){ browser->BackButton(); })),
    forward(this, &menu_atlas->FindGlyph(7)->tex, "", MouseController::CB([&](){ browser->ForwardButton(); })),
    refresh(this, &menu_atlas->FindGlyph(8)->tex, "", MouseController::CB([&](){ browser->RefreshButton(); })),
    address_box(W, FontRef(W, FontDesc(FLAGS_font, "", 12, Color::black, Color::grey90))) {
    address_box.bg_color = Color::grey90;
    address_box.SetToggleKey(0, true);
    address_box.cmd_prefix.clear();
    address_box.deactivate_on_enter = true;
    address_box.runcb = [=](const string &t){ Open(t); };
    Activate();
    Init();
  }

  void Open(const string &url) { app->RunInNetworkThread([=]() { browser->Open(url); }); }

  void Init() {
    InitLayout();
#if LFL_QT
    if (!browser) browser = (qt_browser = CreateQTWebKitBrowser(this, win.w, win.h)).get()
#endif
#ifdef LFL_BERKELIUM
    if (!browser) browser = (berkelium_browser = CreateBerkeliumBrowser(this, win.w, win.h)).get()
#endif
    if (!browser) {
      browser = (lfl_browser = make_unique<Browser>
                 (app, app->focused, app, app->fonts.get(), app->net.get(), app->render_process.get(),
                  app, this, win)).get();
      lfl_browser->doc.js_console = make_unique<JavaScriptConsole>(root, lfl_browser.get());
      lfl_browser->doc.js_console->animating_cb = bind(&MyBrowserWindow::UpdateTargetFPS, this);
      if (app->render_process) lfl_browser->InitLayers(make_unique<LayersIPCServer>(app, app));
      else                     lfl_browser->InitLayers(make_unique<Layers>(app, app));
      if (FLAGS_render_log)    lfl_browser->render_log = &render_log;
      lfl_browser->url_cb = [&](const string &x){ address_box.AssignInput(x); };
    }
    if (!controller) controller = root->AddInputController(make_unique<BrowserController>(root, browser));
    if (root->console) root->console->animating_cb = bind(&MyBrowserWindow::UpdateTargetFPS, this);
    Layout();
  }

  void InitLayout() {
    box = win = root->ViewBox();
    win.SetPosition(point(0, -win.h));
    topbar = win;
    topbar.h = 16;
    topbar.y = win.y + win.h - topbar.h;
    win.h = max(0, win.h - topbar.h);
  }

  View *Layout(Flow *flow_in=nullptr) override {
    ResetView();
    InitLayout();
    addressbar = topbar;
    MinusPlus(&addressbar.w, &addressbar.x, 16*3 + 20);
    child_box.PushBack(topbar, Drawable::Attr(NullPointer<Font>(), &Color::grey70), Singleton<BoxFilled>::Set());

    Flow flow(&box, 0, &child_box);
    flow.cur_attr.font = menu_atlas;
    back   .Layout(&flow, 0, point(16, 16)); flow.p.x += 5;
    forward.Layout(&flow, 0, point(16, 16)); flow.p.x += 5;
    refresh.Layout(&flow, 0, point(16, 16));
    if (lfl_browser) lfl_browser->Layout(win);
    refresh.AddClickBox(addressbar, MouseController::CB([&](){ address_box.Activate(); }));
    return this;
  }

  void Draw(const point &p) override {
    View::Draw(p);
    browser->Draw(box + p);
    address_box.Draw(addressbar + p + box.TopLeft());
    if (lfl_browser) {
      lfl_browser->UpdateScrollbar();
      if (lfl_browser->doc.js_console) lfl_browser->doc.js_console->Draw(p + box.TopLeft());
    }
  }

  int Frame(LFL::Window *W, unsigned clicks, int flag) {
    Draw(W->Box().TopLeft());
    W->DrawDialogs();
    if (FLAGS_render_log && !app->render_process) { printf("Render log: %s\n", render_log.data.c_str()); render_log.Clear(); }
    return 0;
  }

  void ToggleJavaScriptConsole() {
    if (lfl_browser && lfl_browser->doc.js_console) lfl_browser->doc.js_console->ToggleActive();
  }

  void UpdateTargetFPS() { app->scheduler.SetAnimating(root, (root->console && root->console->animating) || 
                                                       (lfl_browser && lfl_browser->doc.js_console->animating)); }
};

void MyWindowInitCB(LFL::Window *W) {
  W->gl_w = FLAGS_width;
  W->gl_h = FLAGS_height;
  W->caption = "Browser";
}

void MyWindowStartCB(LFL::Window *W) {
  MyBrowserWindow *bw = W->AddView(make_unique<MyBrowserWindow>(W));
  W->frame_cb = bind(&MyBrowserWindow::Frame, bw, _1, _2, _3);
  BindMap *binds = W->AddInputController(make_unique<BindMap>());
  binds->Add(Bind('6', Key::Modifier::Cmd, Bind::CB(bind([&](){ W->shell->console(vector<string>()); }))));
  binds->Add(Bind('7', Key::Modifier::Cmd, Bind::CB(bind(&MyBrowserWindow::ToggleJavaScriptConsole, bw))));
}

}; // namespace LFL
using namespace LFL;

extern "C" LFApp *MyAppCreate(int argc, const char* const* argv) {
  FLAGS_enable_video = FLAGS_enable_input = FLAGS_max_rlimit_open_files = 1;
  app = new Application(argc, argv);
  app->focused = app->framework->ConstructWindow(app).release();
  app->window_start_cb = MyWindowStartCB;
  app->window_init_cb = MyWindowInitCB;
  app->window_init_cb(app->focused);
  app->name = "TinyBrowser";
  return app;
}

extern "C" int MyAppMain(LFApp*) {
  if (app->Create(__FILE__)) return -1;
  if (FLAGS_font_engine == "freetype") { DejaVuSansFreetype::SetDefault(); DejaVuSansFreetype::Load(app->fonts.get()); }
  if (app->Init()) return -1;
  app->scheduler.AddMainWaitKeyboard(app->focused);
  app->scheduler.AddMainWaitMouse(app->focused);

  app->net = make_unique<SocketServices>(app, app);
#if !defined(LFL_MOBILE)
  if (FLAGS_render_sandbox) {
    vector<string> arg;
    if (FLAGS_render_log) { arg.push_back("-render_log"); arg.push_back("1"); }
    app->log_pid = true;
    app->render_process = make_unique<ProcessAPIClient>(app, app, app->net.get(), app, app->fonts.get());
    CHECK(app->render_process->StartServerProcess(StrCat(app->bindir, "TinyBrowser-render-sandbox", app->localfs.executable_suffix), arg));
    CHECK(app->CreateNetworkThread(false, false));
    app->net->select_time = Seconds(1).count();
  } else
#endif
  { app->LoadModule(app->net.get()); }

  app->StartNewWindow(app->focused);
  auto bw = app->focused->GetView<MyBrowserWindow>(0);
  if (!FLAGS_url.empty()) bw->Open(FLAGS_url);
  if (app->render_process) {
    app->render_process->browser = bw->lfl_browser.get();
    app->network_thread->thread->Start();
    bw->lfl_browser->SetViewport(bw->lfl_browser->viewport.w, bw->lfl_browser->viewport.h);
  }

  return app->Main();
}
