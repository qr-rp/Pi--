#include "editor.hpp"
#include "tui.hpp"
#include "term.hpp"
#include <algorithm>

namespace pi::tui {

Editor::Editor() { text_.reserve(4096); }
void Editor::set_text(std::string_view t) { text_=t; cursor_x_=cursor_y_=0; line_cache_.clear(); invalidate(); }
void Editor::focus(bool f) { focused_=f; if(f) { cursor_x_=(int)text_.size(); cursor_y_=0; } }

std::vector<std::string> Editor::compute_lines(int width) const {
    if(cache_width_==width&&!line_cache_.empty()) return line_cache_;
    line_cache_.clear(); cache_width_=width;
    int avail=std::max(1,width-6);
    if(text_.empty()){line_cache_.push_back("");return line_cache_;}
    std::string_view sv(text_); size_t pos=0;
    while(pos<sv.size()){
        auto nl=sv.find('\n',pos);
        size_t end=(nl==std::string_view::npos)?sv.size():nl;
        std::string_view seg(sv.data()+pos,end-pos);
        size_t start=0;
        while(start<seg.size()){size_t ch=std::min(start+avail,seg.size());line_cache_.push_back(std::string(seg.substr(start,ch-start)));start=ch;}
        if(nl!=std::string_view::npos){if(end==pos)line_cache_.push_back("");pos=nl+1;}else break;
    }
    if(line_cache_.empty())line_cache_.push_back("");return line_cache_;
}

int Editor::cursor_index() const {
    if(text_.empty())return 0;int ln=0,col=0;
    for(size_t i=0;i<text_.size();++i){if(ln==cursor_y_&&col==cursor_x_)return(int)i;if(text_[i]=='\n'){ln++;col=0;}else col++;}
    return(int)text_.size();
}
void Editor::cursor_from_index(int idx){cursor_y_=cursor_x_=0;for(int i=0;i<idx&&i<(int)text_.size();++i){if(text_[i]=='\n'){cursor_y_++;cursor_x_=0;}else cursor_x_++;}}
void Editor::insert(char c){int idx=cursor_index();if(c=='\n'){text_.insert(text_.begin()+idx,'\n');cursor_y_++;cursor_x_=0;}else{text_.insert(text_.begin()+idx,c);cursor_x_++;}line_cache_.clear();invalidate();}
void Editor::backspace(){if(text_.empty()||(cursor_y_==0&&cursor_x_==0))return;int idx=cursor_index();if(idx<=0)return;if(text_[idx-1]=='\n'){cursor_y_--;int pn=(int)text_.rfind('\n',idx-2);cursor_x_=(pn<0)?idx-1:idx-pn-2;}else cursor_x_--;text_.erase(idx-1,1);line_cache_.clear();invalidate();}
void Editor::del(){if(text_.empty())return;int idx=cursor_index();if(idx>=(int)text_.size())return;text_.erase(idx,1);line_cache_.clear();invalidate();}
void Editor::move_left(){if(cursor_x_>0){cursor_x_--;return;}if(cursor_y_>0){cursor_y_--;cursor_x_=0;int ci=cursor_index();while(ci<(int)text_.size()&&text_[ci]!='\n'){cursor_x_++;ci++;}}}
void Editor::move_right(){int idx=cursor_index();if(idx>=(int)text_.size())return;if(text_[idx]=='\n'){cursor_y_++;cursor_x_=0;}else cursor_x_++;}
void Editor::move_up(){if(cursor_y_<=0)return;cursor_y_--;int idx=cursor_index(),mx=0;while(idx<(int)text_.size()&&text_[idx]!='\n'){mx++;idx++;}if(cursor_x_>mx)cursor_x_=mx;}
void Editor::move_down(){int idx=cursor_index();while(idx<(int)text_.size()&&text_[idx]!='\n')idx++;if(idx>=(int)text_.size())return;idx++;cursor_y_++;int mx=0;while(idx<(int)text_.size()&&text_[idx]!='\n'){mx++;idx++;}if(cursor_x_>mx)cursor_x_=mx;}
void Editor::move_home(){cursor_x_=0;}
void Editor::move_end(){int idx=cursor_index();cursor_x_=0;while(idx<(int)text_.size()&&text_[idx]!='\n'){cursor_x_++;idx++;}}

std::vector<std::string> Editor::render(int width) {
    auto lines=compute_lines(width);std::vector<std::string> res;int ln=1;
    for(auto& l:lines){std::string p=focused_?term::fg(term::GRAY)+std::format("{:>3}|",ln)+term::RESET:std::format("{:>3} ",ln);std::string line=p+l;if((int)line.size()<width)line.append(width-line.size(),' ');res.push_back(line);ln++;}
    while((int)res.size()<edit_height_){std::string p=focused_?term::fg(term::GRAY)+std::format("{:>3}|",ln)+term::RESET:std::format("{:>3} ",ln);res.push_back(p+std::string(std::max(0,width-(int)p.size()),' '));ln++;}
    std::string bdr(width,'-');res.push_back(focused_?term::fg(term::GRAY)+bdr+term::RESET:bdr);return res;
}

bool Editor::handle_input(const InputEvent& ev) {
    if(!focused_)return false;

    // Navigation (omp TUI_KEYBINDINGS)
    if(ev.key==Key::Up){
        if(history_.empty())return true;
        if(history_pos_>0){if(history_pos_==(int)history_.size())saved_input_=text_;history_pos_--;set_text(history_[history_pos_]);}
        return true;
    }
    if(ev.key==Key::Down){
        if(history_pos_<(int)history_.size()-1){history_pos_++;set_text(history_[history_pos_]);}
        else if(history_pos_==(int)history_.size()-1){history_pos_=(int)history_.size();set_text(saved_input_);}
        return true;
    }
    if(ev.key==Key::Left||(ev.key==Key::CtrlB&&!ev.alt)){move_left();return true;}
    if(ev.key==Key::Right||(ev.key==Key::CtrlF&&!ev.alt)){move_right();return true;}
    if(ev.key==Key::Home||ev.key==Key::CtrlA){move_home();return true;}
    if(ev.key==Key::End||ev.key==Key::CtrlE){move_end();return true;}

    // Word movement: Alt+B/F, Alt+left/right (omp: cursorWordLeft/Right)
    if((ev.alt&&(ev.ch=='b'||ev.ch=='B'))){
        int idx=cursor_index(),st=idx;while(st>0&&text_[st-1]==' ')st--;while(st>0&&text_[st-1]!=' '&&text_[st-1]!='\n')st--;
        cursor_from_index(st);return true;
    }
    if((ev.alt&&(ev.ch=='f'||ev.ch=='F'))){
        int idx=cursor_index(),en=idx;while(en<(int)text_.size()&&text_[en]!=' '&&text_[en]!='\n')en++;
        while(en<(int)text_.size()&&text_[en]==' ')en++;cursor_from_index(en);return true;
    }

    // Deletion (omp)
    if(ev.key==Key::Backspace||ev.key==Key::CtrlH){backspace();return true;}
    if(ev.key==Key::Delete||ev.key==Key::CtrlD){del();return true;}
    if(ev.key==Key::CtrlW||(ev.alt&&ev.ch==127)||(ev.ctrl&&ev.key==Key::Backspace)){
        int idx=cursor_index(),st=idx;while(st>0&&text_[st-1]==' ')st--;while(st>0&&text_[st-1]!=' ')st--;
        text_.erase(st,idx-st);cursor_from_index(st);line_cache_.clear();invalidate();return true;
    }
    if((ev.alt&&ev.ch=='d')||(ev.alt&&ev.key==Key::Delete)){
        int idx=cursor_index(),en=idx;while(en<(int)text_.size()&&text_[en]!=' '&&text_[en]!='\n')en++;
        while(en<(int)text_.size()&&text_[en]==' ')en++;text_.erase(idx,en-idx);line_cache_.clear();invalidate();return true;
    }
    if(ev.key==Key::CtrlK){ // deleteToLineEnd
        int idx=cursor_index();auto nl=text_.find('\n',idx);
        if(nl==std::string_view::npos)text_.erase(idx);else text_.erase(idx,nl-idx);
        line_cache_.clear();invalidate();return true;
    }
    if(ev.key==Key::CtrlU){ // deleteToLineStart
        int idx=cursor_index(),sol=(int)text_.rfind('\n',idx-1);
        if(sol<0)sol=0;else sol++;text_.erase(sol,idx-sol);cursor_from_index(sol);
        line_cache_.clear();invalidate();return true;
    }

    // Yank / Undo (stubs for now)
    if(ev.key==Key::CtrlY){return true;}
    if((ev.alt&&ev.ch=='y')){return true;}
    // undo stub removed - Ctrl+_ and Ctrl+- not easily parsed
    // Tab / autocomplete
    if(ev.key==Key::Tab){
        int idx=cursor_index(),st=idx;while(st>0&&text_[st-1]!=' '&&text_[st-1]!='\n')st--;
        std::string pref=text_.substr(st,idx-st);
        if(on_tab&&!pref.empty()){auto c=on_tab(pref);if(c.size()==1){text_.erase(st,idx-st);text_.insert(st,c[0]);cursor_from_index(st+(int)c[0].size());line_cache_.clear();invalidate();return true;}}
        insert(' ');return true;
    }

    // Submit (Enter = submit, Shift+Enter = newline per omp)
    if(ev.key==Key::Enter){
        if(ev.alt){/*Alt+Enter=followUp in omp === submit */;}
        if(on_submit&&!text_.empty()){
            history_.push_back(text_);history_pos_=(int)history_.size();
            text_.clear();cursor_x_=cursor_y_=0;line_cache_.clear();invalidate();
            on_submit(history_.back());
        }
        return true;
    }
    if(ev.key==Key::ShiftTab){return true;}

    // UTF-8 / printable
    if(!ev.text.empty()){int idx=cursor_index();text_.insert(idx,ev.text);cursor_x_++;line_cache_.clear();invalidate();return true;}
    if(ev.ch>=0x20){insert(ev.ch);return true;}
    return true;
}

} // namespace pi::tui
