"""RTAL HQ FLANGER Editor V1.0.0 FINAL — release candidate."""
import argparse
from tkinter import messagebox
import time
import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import json
import math
import os
import re
from visual_dsp_model import delays, response, wave
from pathlib import Path
import mido
PREFIX=[0x7D,0x52,0x54,0x46]
NAMES=['TIME','FEEDBACK','LFO RATE','LFO AMOUNT','MIX','DRIVE','OUTPUT LEVEL','BBD TONE','BBD SAT','LFO WAVE','LFO SMOOTH','SYNC ENABLE','SYNC DIV','INT BPM','RESET START','BBD MODEL','FB COLOR','BBD NOISE','STEREO WIDTH','STEREO PHASE','MODE','BYPASS','STEREO MODE','INPUT','LFO2 RATE','LFO2 DEPTH','LFO2 WAVE','LFO2 TARGET','CROSS FEEDBACK','CHARACTER','DUAL TAP','ENVELOPE','FEEDBACK DAMP','STEREO MOTION','THROUGH ZERO','FEEDBACK MATRIX','ENV ATTACK','ENV RELEASE','ENV TARGET']
def nibble(data):
    return [part for byte in data for part in (byte&15,byte>>4)]
def unpack(data):
    if len(data)%2 or any(x>15 for x in data): raise ValueError('Bad nibble encoding')
    return [data[i]|(data[i+1]<<4) for i in range(0,len(data),2)]

GROUPS = {
 'MAIN': [0,1,2,3,4,5,6,21,23],
 'BBD / CHARACTER': [7,8,15,16,17,29,30,32],
 'LFO / CLOCK': [9,10,11,12,13,14,24,25,26,27],
 'STEREO / ROUTING': [18,19,20,22,28,33,34,35],
 'ENVELOPE': [31,36,37,38],
}
BOUNDS={13:(40,240),9:(0,6),12:(0,11),11:(0,1),14:(0,1),21:(0,1),26:(0,3),27:(0,3),35:(0,3),38:(0,3),29:(0,2)}
def settings_path():
    return Path.home()/'.rtal_hq_flanger_editor_settings.json'

def load_settings():
    try:
        value=json.loads(settings_path().read_text(encoding='utf-8'))
        return value if isinstance(value,dict) else {}
    except (OSError,ValueError,TypeError):
        return {}

def save_settings(data):
    path=settings_path();tmp=path.with_name(path.name+'.tmp')
    try:
        tmp.write_text(json.dumps(data,indent=2),encoding='utf-8')
        os.replace(tmp,path)
    except OSError:
        try:tmp.unlink(missing_ok=True)
        except OSError:pass

class App:
    def __init__(self,root,inp,out):
        self.root=root; self.inp=mido.open_input(inp); self.out=mido.open_output(out)
        self.vars=[];self.controls=[];self.pending={};self.last_poll=0;self.loading=True
        self._closing=False;self._last_midi_error=0;self._settings=load_settings()
        self._last_device_reply=time.monotonic()
        root.title('RTAL HQ FLANGER | Editor V1.0.0 FINAL')
        screen_w,screen_h=root.winfo_screenwidth(),root.winfo_screenheight()
        width=min(1480,max(900,screen_w-80))
        height=min(960,max(650,screen_h-100))
        default_geometry=f'{width}x{height}+{max(0,(screen_w-width)//2)}+{max(0,(screen_h-height)//2)}'
        saved_geometry=self._settings.get('geometry','')
        match=re.fullmatch(r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)',saved_geometry)
        if match:
            gw,gh,gx,gy=map(int,match.groups())
            if 880<=gw<=screen_w and 620<=gh<=screen_h and 0<=gx<screen_w-100 and 0<=gy<screen_h-100:
                default_geometry=saved_geometry
        root.geometry(default_geometry)
        root.minsize(880,620)
        style=ttk.Style(root)
        if 'clam' in style.theme_names():style.theme_use('clam')
        style.configure('TFrame',background='#111b29');style.configure('TLabel',background='#111b29',foreground='#dce9f9',font=('Segoe UI',10))
        style.configure('Title.TLabel',font=('Segoe UI',19,'bold'),foreground='#80d7ff')
        style.configure('Brand.TLabel',font=('Segoe UI',10,'italic'),foreground='#a9bed6')
        style.configure('TButton',padding=(10,8))
        style.configure('TLabelframe',background='#111b29',foreground='#80d7ff')
        style.configure('TLabelframe.Label',background='#111b29',foreground='#80d7ff',font=('Segoe UI',11,'bold'))
        style.configure('TNotebook',background='#111b29');style.configure('TNotebook.Tab',padding=(18,9),font=('Segoe UI',10,'bold'))
        style.configure('TScale',background='#111b29')
        top=ttk.Frame(root,padding=16);top.pack(fill='x')
        branding=ttk.Frame(top);branding.pack(side='left',fill='x',expand=True)
        ttk.Label(branding,text='HQ DIGITAL FLANGER',style='Title.TLabel').pack(anchor='w')
        ttk.Label(branding,text='RealTimeAudioLab',style='Brand.TLabel').pack(anchor='w')
        ttk.Button(top,text='READ ALL',command=lambda:self.send(3)).pack(side='right',padx=6)
        ttk.Button(top,text='CHANGE MIDI PORTS',command=self.change_ports).pack(side='right',padx=6)
        ttk.Button(top,text='RECONNECT',command=self.reconnect_midi).pack(side='right',padx=6)
        self.status=tk.StringVar(value='Connecting to flanger…')
        ttk.Label(root,textvariable=self.status,padding=(18,4)).pack(fill='x')
        self.note=note=ttk.Notebook(root);note.pack(fill='both',expand=True,padx=12,pady=12)
        root.bind('<F5>',lambda _event:self.send(3))
        self.controls=[None]*len(NAMES);self.vars=[tk.IntVar(value=0) for _ in NAMES]
        for group,indices in GROUPS.items():
            page=ttk.Frame(note,padding=18);note.add(page,text=group)
            page.columnconfigure(1,weight=1)
            for row,i in enumerate(indices):
                lo,hi=BOUNDS.get(i,(0,100))
                ttk.Label(page,text=NAMES[i],width=23).grid(row=row,column=0,sticky='w',padx=(0,10),pady=11)
                scale=ttk.Scale(page,from_=lo,to=hi,orient='horizontal',command=lambda x,index=i:self.on_slider(index,x))
                scale.grid(row=row,column=1,sticky='ew',padx=12)
                ttk.Label(page,textvariable=self.vars[i],width=6).grid(row=row,column=2,sticky='e')
                self.controls[i]=(scale,lo,hi)
        preset_page=ttk.Frame(note,padding=18);note.add(preset_page,text='PRESETS')
        ttk.Label(preset_page,text='PRESET BROWSER 001–128',style='Title.TLabel').pack(anchor='w',pady=(0,12))
        ttk.Label(preset_page,text='Program Change loads presets; names can now be read from the Flanger RAM cache.').pack(anchor='w',pady=(0,12))
        self.preset_number=tk.IntVar(value=1)
        self.preset_label=tk.StringVar(value='')
        self.labels_path=Path.home()/'.rtal_hq_flanger_preset_labels.json'
        try:self.labels=json.loads(self.labels_path.read_text(encoding='utf-8'))
        except (OSError,ValueError):self.labels={}
        row=ttk.Frame(preset_page);row.pack(fill='x',pady=10)
        ttk.Label(row,text='Slot (1–128):').pack(side='left',padx=(0,12))
        slot=ttk.Spinbox(row,from_=1,to=128,textvariable=self.preset_number,width=8,command=self.show_preset_label)
        slot.pack(side='left')
        slot.bind('<Return>',lambda e:self.show_preset_label())
        ttk.Button(row,text='LOAD ON FLANGER',command=self.load_preset).pack(side='left',padx=14)
        ttk.Label(preset_page,text='Preset name on device:').pack(anchor='w',pady=(16,4))
        ttk.Entry(preset_page,textvariable=self.preset_label,width=55).pack(anchor='w')
        ttk.Button(preset_page,text='READ NAME FROM FLANGER',command=self.read_device_name).pack(anchor='w',pady=10)
        ttk.Button(preset_page,text='READ ALL 128 NAMES',command=self.read_all_names).pack(anchor='w',pady=5)
        ttk.Button(preset_page,text='SAVE ACTIVE PRESET TO SD',command=self.save_device_preset).pack(anchor='w',pady=6)
        ttk.Button(preset_page,text='RENAME ON FLANGER',command=self.rename_device_preset).pack(anchor='w',pady=6)
        ttk.Label(preset_page,text='CAUTION: Backup SD /RTAL_FLANGER before writing. Save requires the selected slot to be active.').pack(anchor='w',pady=15)
        morph_page=ttk.Frame(note,padding=18);note.add(morph_page,text='MORPH CONTROL')
        ttk.Label(morph_page,text='MORPH CONFIGURATION — PC CONTROL',style='Title.TLabel').pack(anchor='w',pady=(0,10))
        self.morph_display=tk.StringVar(value='Awaiting morph snapshot…')
        self.morph_vars=[tk.IntVar(value=v) for v in (1,2,0,35,0,0,0,0,4,0)]
        fields=[('PRESET A',1,128),('PRESET B',1,128),('POSITION %',0,100),('TIME (0–100)',0,100),
                ('MODE (0=MANUAL 1=AUTO)',0,1),('CURVE (0=LINEAR 1=SMOOTH)',0,1),
                ('PING-PONG',0,1),('MIDI SYNC',0,1),('DIVISION INDEX',0,7),('START DIRECTION (0=FWD 1=REV)',0,1)]
        for label,lo,hi in fields:
            i=len(getattr(self,'_morph_built',[]));self._morph_built=getattr(self,'_morph_built',[])+[1]
            row=ttk.Frame(morph_page);row.pack(fill='x',pady=2)
            ttk.Label(row,text=label,width=30).pack(side='left')
            ttk.Spinbox(row,from_=lo,to=hi,textvariable=self.morph_vars[i],width=9).pack(side='left')
        buttons=ttk.Frame(morph_page);buttons.pack(anchor='w',pady=12)
        ttk.Button(buttons,text='READ MORPH',command=lambda:self.send(13)).pack(side='left',padx=4)
        ttk.Button(buttons,text='APPLY TO FLANGER',command=self.apply_morph).pack(side='left',padx=4)
        ttk.Button(buttons,text='SAVE MORPH TO SD',command=self.save_morph).pack(side='left',padx=4)
        transport=ttk.LabelFrame(morph_page,text='LIVE MORPH TRANSPORT',padding=10)
        transport.pack(fill='x',pady=8)
        self.transport_state=tk.StringVar(value='STOPPED / awaiting device')
        self.transport_position=tk.DoubleVar(value=0)
        ttk.Button(transport,text='▶ PLAY',command=lambda:self.morph_transport(0)).pack(side='left',padx=5)
        ttk.Button(transport,text='■ STOP',command=lambda:self.morph_transport(1)).pack(side='left',padx=5)
        ttk.Button(transport,text='⇄ REVERSE',command=lambda:self.morph_transport(2)).pack(side='left',padx=5)
        ttk.Label(transport,textvariable=self.transport_state).pack(side='left',padx=12)
        ttk.Progressbar(morph_page,maximum=100,variable=self.transport_position,length=500).pack(fill='x',pady=4)
        ttk.Label(morph_page,textvariable=self.morph_display,font=('Consolas',10),justify='left').pack(anchor='w',pady=8)
        ttk.Label(morph_page,text='Apply changes first; SAVE writes only the active preset’s morph configuration. Backup SD before testing.').pack(anchor='w')
        self.visual_page=ttk.Frame(note,padding=14)
        note.add(self.visual_page,text='VISUAL DSP')
        ttk.Label(self.visual_page,text='STEREO FLANGER  |  VISUAL DSP',style='Title.TLabel').pack(anchor='w')
        ttk.Label(self.visual_page,text='PC-side model from synchronized controls · illustrative response, not measured audio').pack(anchor='w',pady=(2,8))
        self.visual_canvas=tk.Canvas(self.visual_page,height=470,bg='#07111f',highlightthickness=0)
        self.visual_canvas.pack(fill='both',expand=True)
        self.visual_info=tk.StringVar(value='Waiting for parameter synchronization')
        ttk.Label(self.visual_page,textvariable=self.visual_info,font=('Consolas',10)).pack(anchor='w',pady=8)
        self.visual_phase=0.0
        self.visual_last=time.monotonic()
        self.visual_last_draw=0.0
        self.visual_freeze=tk.BooleanVar(value=False)
        self.visual_resolution=tk.StringVar(value=self._settings.get('resolution','HIGH') if self._settings.get('resolution') in ('ECO','HIGH') else 'HIGH')
        self.visual_hold=None
        self.visual_show_hold=tk.BooleanVar(value=True)
        self.visual_show_lfo2=tk.BooleanVar(value=True)
        self.visual_phase2=0.0
        self.visual_stereo_difference=tk.BooleanVar(value=False)
        self.visual_peak_markers=tk.BooleanVar(value=False)
        self.visual_morph_preview=tk.BooleanVar(value=False)
        self.visual_a=None
        self.visual_b=None
        self.preset_visual_cache={}
        self.preset_fetch_queue=[]
        self.preset_fetch_active=None
        self.preset_fetch_sent=0.0
        self.preset_fetch_supported=True
        self.visual_auto_info=tk.StringVar(value="A/B: waiting for preset snapshots; arbitrary device preset fetch is not supported")
        self.visual_render_cache=None
        self.visual_show_grid=tk.BooleanVar(value=bool(self._settings.get('grid',True)))
        self.visual_fps=tk.StringVar(value=str(self._settings.get('fps','24')) if str(self._settings.get('fps','24')) in ('12','24','30') else '24')
        # Two uniform, responsive action rows; toggles live in separate option rows.
        actions=ttk.Frame(self.visual_page);actions.pack(fill='x',pady=(5,2))
        for col in range(5):actions.columnconfigure(col,weight=1,uniform='visual_actions')
        action_items=[('CAPTURE A/B',self.visual_capture),('CLEAR A/B',self.visual_clear),
                      ('FETCH MORPH A/B',self.fetch_morph_endpoints),
                      ('EXPORT PNG',self.visual_export),('EXPORT SVG',self.visual_export_svg)]
        for col,(label,command) in enumerate(action_items):
            ttk.Button(actions,text=label,command=command).grid(row=0,column=col,sticky='ew',padx=3,pady=3)
        morph_actions=ttk.Frame(self.visual_page);morph_actions.pack(fill='x',pady=(0,3))
        for col in range(5):morph_actions.columnconfigure(col,weight=1,uniform='morph_actions')
        ttk.Button(morph_actions,text='SET MORPH A',command=lambda:self.visual_set_endpoint('A')).grid(row=0,column=0,sticky='ew',padx=3)
        ttk.Button(morph_actions,text='SET MORPH B',command=lambda:self.visual_set_endpoint('B')).grid(row=0,column=1,sticky='ew',padx=3)
        ttk.Checkbutton(morph_actions,text='MORPH PREVIEW (PC)',variable=self.visual_morph_preview).grid(row=0,column=2,sticky='w',padx=7)
        ttk.Checkbutton(morph_actions,text='FREEZE',variable=self.visual_freeze).grid(row=0,column=3,sticky='w',padx=7)
        ttk.Checkbutton(morph_actions,text='COMPARE',variable=self.visual_show_hold).grid(row=0,column=4,sticky='w',padx=7)
        options=ttk.Frame(self.visual_page);options.pack(fill='x',pady=(0,4))
        for text,var in [('LFO 2',self.visual_show_lfo2),('STEREO Δ',self.visual_stereo_difference),
                         ('PEAK MARKERS',self.visual_peak_markers),('GRID',self.visual_show_grid)]:
            ttk.Checkbutton(options,text=text,variable=var).pack(side='left',padx=7)
        ttk.Label(options,text='RESOLUTION').pack(side='left',padx=(15,4))
        ttk.Combobox(options,textvariable=self.visual_resolution,values=('ECO','HIGH'),state='readonly',width=7).pack(side='left')
        ttk.Label(options,text='FPS').pack(side='left',padx=(15,4))
        ttk.Combobox(options,textvariable=self.visual_fps,values=('12','24','30'),width=4,state='readonly').pack(side='left')
        ttk.Label(self.visual_page,textvariable=self.visual_auto_info,font=('Consolas',9)).pack(anchor='w',pady=(4,0))
        self.name_queue=[];self.name_next=0.0
        self.show_preset_label()
        footer=ttk.Label(root,text='Live bidirectional SysEx  •  39 parameters  •  Firmware 1.0.0  •  Visual DSP on PC',padding=(18,10))
        footer.pack(fill='x')
        self.loading=False
        note.select(self.visual_page)
        root.protocol('WM_DELETE_WINDOW',self.close)
        self.send(3);self.send(13);root.after(40,self.tick)
    def fetch_morph_endpoints(self):
        try:a=int(self.morph_vars[0].get());b=int(self.morph_vars[1].get())
        except (ValueError,tk.TclError):return
        if not (1<=a<=128 and 1<=b<=128):return
        self.preset_fetch_queue=[n for n in dict.fromkeys((a,b)) if n not in self.preset_visual_cache]
        self.preset_fetch_active=None
        if not self.preset_fetch_queue:self.visual_auto_endpoints()
        else:self.visual_auto_info.set('Requesting stored presets from device RAM cache (no preset switch)…')

    def visual_auto_endpoints(self,silent=False):
        """Use only snapshots confirmed after loading via editor; never silently load device presets."""
        a,b=self.morph_vars[0].get(),self.morph_vars[1].get()
        missing=[n for n in (a,b) if n not in self.preset_visual_cache]
        if missing:
            info='A/B snapshots missing for '+', '.join(f'{n:03d}' for n in missing)+'. Load each preset in PRESETS first.'
            self.visual_auto_info.set(info)
            if not silent:messagebox.showinfo('Morph endpoint cache',info)
            return
        self.visual_a=list(self.preset_visual_cache[a]);self.visual_b=list(self.preset_visual_cache[b])
        self.visual_morph_preview.set(True)
        self.visual_auto_info.set(f'AUTO MORPH A={a:03d} B={b:03d} · captured device parameter snapshots (session)')
        self.status.set('Morph endpoints loaded from local session cache; no device write')

    def morph_transport(self,action):
        self.send(0x13,[action]);self.status.set('Morph transport command sent; awaiting acknowledgement')
    def update_transport(self,result):
        if len(result)!=7:return
        slot,status,running,pos,reverse,mode,active=result
        self.transport_position.set(pos)
        self.transport_state.set(f'{"PLAYING" if running else "STOPPED"}  {pos}%  {"REVERSE" if reverse else "FORWARD"}')
        if status:self.status.set(f'Morph transport error {status} (preset {slot:03d})')
        self.morph_vars[2].set(pos)
        
    def apply_morph(self):
        try:values=[v.get() for v in self.morph_vars]
        except (ValueError,tk.TclError):return
        limits=[(1,128),(1,128),(0,100),(0,100),(0,1),(0,1),(0,1),(0,1),(0,7),(0,1)]
        if any(not lo<=v<=hi for v,(lo,hi) in zip(values,limits)):
            messagebox.showerror('Morph','One or more morph values are out of range.');return
        self.send(15,values);self.status.set('Applying morph configuration — awaiting confirmation')
    def save_morph(self):
        if not messagebox.askyesno('Save morph','Save current morph configuration to active preset on SD?') :return
        self.send(17);self.morph_save_deadline=time.monotonic()+4.0;self.status.set('Saving morph configuration — awaiting confirmation (4s timeout)')
    def save_device_preset(self):
        try:slot=int(self.preset_number.get())
        except (ValueError,tk.TclError):return
        if not 1<=slot<=128:return
        if not messagebox.askyesno('Save preset',f'Overwrite preset {slot:03d} on SD?\nOnly the active device preset can be saved.') :return
        self.send(9,[slot]);self.status.set(f'Saving preset {slot:03d} — awaiting device confirmation')
    def rename_device_preset(self):
        try:slot=int(self.preset_number.get())
        except (ValueError,tk.TclError):return
        name=self.preset_label.get().strip()
        if not 1<=slot<=128 or not 1<=len(name)<=19 or any(ord(c)<32 or ord(c)>126 or c in '/\\' for c in name):
            messagebox.showerror('Name','Use 1–19 printable ASCII characters, without / or backslash.');return
        if not messagebox.askyesno('Rename preset',f'Rename preset {slot:03d} on SD to {name!r}?'):return
        self.send(11,[slot]+list(name.encode('ascii')))
        self.status.set(f'Renaming preset {slot:03d} — awaiting device confirmation')
    def read_device_name(self):
        try: n=int(self.preset_number.get())
        except (ValueError,tk.TclError): return
        if 1<=n<=128:self.name_queue=[n]
    def read_all_names(self):
        self.name_queue=list(range(1,129))
        self.status.set('Reading 128 preset names from Flanger…')
    def show_preset_label(self):
        try:n=int(self.preset_number.get())
        except (ValueError,tk.TclError):return
        if 1<=n<=128:self.preset_label.set(self.labels.get(str(n),''))
    def save_preset_label(self):
        try:n=int(self.preset_number.get())
        except (ValueError,tk.TclError):return
        if not 1<=n<=128:return
        self.labels[str(n)]=self.preset_label.get()
        try:self.labels_path.write_text(json.dumps(self.labels,indent=2,ensure_ascii=False),encoding='utf-8')
        except OSError as exc:messagebox.showerror('Label save error',str(exc))
        else:self.status.set(f'Local label for slot {n:03d} saved (PC only)')
    def load_preset(self):
        try:n=int(self.preset_number.get())
        except (ValueError,tk.TclError):return
        if not 1<=n<=128:
            messagebox.showerror('Preset','Select slot 1–128');return
        self.out.send(mido.Message('program_change',channel=0,program=n-1))
        self.status.set(f'Program Change {n-1} sent on MIDI channel 1 (preset {n:03d})')
        self._preset_snapshot_pending=n
        self.root.after(250,lambda:self.send(3))
        self.root.after(400,lambda:self.send(13))
    def ports(self):
        messagebox.showinfo('MIDI ports','INPUTS:\n'+'\n'.join(mido.get_input_names())+'\n\nOUTPUTS:\n'+'\n'.join(mido.get_output_names()))
    def send(self,cmd,payload=()):
        self.out.send(mido.Message('sysex',data=PREFIX+[cmd,0]+nibble(payload)))
    def on_slider(self,index,x):
        value=round(float(x));self.vars[index].set(value)
        if not self.loading:self.pending[index]=value
    def tick(self):
        try:
            for msg in self.inp.iter_pending():
                if msg.type!='sysex':continue
                d=list(msg.data)
                if d[:4]!=PREFIX or len(d)<6:continue
                if d[4]==4:
                    try:vals=unpack(d[6:])
                    except ValueError:continue
                    if len(vals)!=len(NAMES):continue
                    self.loading=True
                    self._last_device_reply=time.monotonic()
                    for i,v in enumerate(vals):
                        if i not in self.pending:
                            self.vars[i].set(v);self.controls[i][0].set(v)
                    self.loading=False;self.status.set('CONNECTED — 39 parameters synchronized')
                    slot=getattr(self,'_preset_snapshot_pending',None)
                    if slot is not None and not self.pending:
                        self.preset_visual_cache[slot]=tuple(vals)
                        self._preset_snapshot_pending=None
                        self.visual_auto_info.set(f'Cached preset {slot:03d} (session only). Use AUTO A/B FROM CACHE when both endpoints visited.')
                elif d[4]==0x18:
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    if len(result)<2:continue
                    slot,status=result[:2]
                    if self.preset_fetch_active!=slot:continue
                    if status==5:
                        self.preset_fetch_sent=time.monotonic()-0.7
                        continue
                    self.preset_fetch_active=None
                    if status!=0 or len(result)!=2+len(NAMES):
                        self.preset_fetch_queue=[]
                        self.visual_auto_info.set(f'Preset {slot:03d} unavailable (status {status}). Firmware FIX7 required.')
                        continue
                    self.preset_visual_cache[slot]=tuple(result[2:])
                    self.visual_auto_info.set(f'Fetched stored preset {slot:03d} without switching audio preset')
                    if not self.preset_fetch_queue:self.visual_auto_endpoints(silent=True)
                elif d[4]==8:
                    try: result=unpack(d[6:])
                    except ValueError: continue
                    if len(result)!=21 or not 1<=result[0]<=128:continue
                    slot=result[0]
                    name=bytes(result[1:]).split(b'\0',1)[0].decode('ascii',errors='replace')
                    self.labels[str(slot)]=name
                    if slot==self.preset_number.get():self.preset_label.set(name)
                    self.status.set(f'Device preset {slot:03d}: {name}')
                elif d[4]==14:
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    if len(result)!=12:continue
                    slot,a,b,pos,tm,mode,curve,ping,sync,division,reverse,saved=result
                    for var,val in zip(self.morph_vars,(a,b,pos,tm,mode,curve,ping,sync,division,reverse)):var.set(val)
                    self.morph_display.set(
                        f'ACTIVE PRESET   {slot:03d}    SAVED MORPH: {"YES" if saved else "NO"}\n'
                        f'PRESET A / B    {a:03d} / {b:03d}\n'
                        f'POSITION        {pos}%\nTIME            {tm} (firmware value)\n'
                        f'MODE            {"AUTO" if mode else "MANUAL"}\n'
                        f'CURVE           {"SMOOTH" if curve else "LINEAR"}\n'
                        f'PING-PONG       {"ON" if ping else "OFF"}\n'
                        f'MIDI SYNC       {"ON" if sync else "OFF"}\n'
                        f'DIVISION        {division} (firmware index)\n'
                        f'START DIRECTION {"REVERSE" if reverse else "FORWARD"}')
                    self.status.set(f'Morph configuration read for preset {slot:03d}')
                    if self.visual_morph_preview.get():self.visual_auto_endpoints(silent=True)
                elif d[4] in (0x14,0x16):
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    self.update_transport(result)
                elif d[4] in (16,18):
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    if len(result)!=2:continue
                    slot,status=result
                    action='Morph apply' if d[4]==16 else 'Morph save'
                    if d[4]==18:self.morph_save_deadline=None
                    self.status.set(f'{action} preset {slot:03d}: '+('confirmed' if status==0 else f'FAILED (code {status})'))
                    if status==0:self.send(13)
                elif d[4] in (10,12):
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    if len(result)!=2:continue
                    slot,status=result
                    operation='Save' if d[4]==10 else 'Rename'
                    self.status.set(f'{operation} preset {slot:03d}: '+('confirmed' if status==0 else f'FAILED (code {status})'))
                    if status==0:self.name_queue.append(slot)
                elif d[4]==6:
                    try:result=unpack(d[6:])
                    except ValueError:continue
                    if len(result)==2 and result[1]:self.status.set(f'Parameter {result[0]} rejected by firmware')
            now=time.monotonic()
            if getattr(self,"morph_save_deadline",None) and now>self.morph_save_deadline:
                self.morph_save_deadline=None
                self.status.set("MORPH SAVE TIMEOUT: no device acknowledgement; inspect USB serial diagnostics")
            if self.preset_fetch_active is not None and now-self.preset_fetch_sent>1.2:
                self.visual_auto_info.set('Preset read timeout — requires firmware C2 FIX7; no preset was switched.')
                self.preset_fetch_active=None
                self.preset_fetch_queue=[]
            if self.preset_fetch_active is None and self.preset_fetch_queue:
                slot=self.preset_fetch_queue.pop(0)
                self.preset_fetch_active=slot
                self.preset_fetch_sent=now
                self.send(0x17,[slot])
            if self.name_queue and now>=self.name_next:
                slot=self.name_queue.pop(0)
                self.send(7,[slot]);self.name_next=now+0.08
            if self.pending:
                index,value=self.pending.popitem();self.send(5,[index,value])
            elif now-self.last_poll>0.35:
                self.send(3);self.last_poll=now
            if now-getattr(self,'last_transport_poll',0)>0.15:
                self.send(0x15);self.last_transport_poll=now
        except Exception as exc:
            if time.monotonic()-self._last_midi_error>2.0:
                self.status.set('MIDI error: '+str(exc)+' — use RECONNECT or CHANGE MIDI PORTS')
                self._last_midi_error=time.monotonic()
        if self.visual_freeze.get():
            # True freeze: keep the entire canvas and its labels as a snapshot.
            # Reset the time reference so UNFREEZE never jumps forward.
            self.visual_last=now
        elif now-self.visual_last_draw>1.0/max(1,int(self.visual_fps.get())):
            self.draw_visual(now)
            self.visual_last_draw=now
        if not self._closing:
            self.root.after(40,self.tick)
    def visual_set_endpoint(self,endpoint):
        values=[x.get() for x in self.vars]
        if endpoint=='A':self.visual_a=values
        else:self.visual_b=values
        self.status.set(f'VISUAL DSP: endpoint {endpoint} captured (PC-only)')

    def visual_export_svg(self):
        from tkinter import filedialog
        import html
        path=filedialog.asksaveasfilename(defaultextension='.svg',filetypes=[('SVG vector image','*.svg')])
        if not path:return
        c=self.visual_canvas
        w=max(450,c.winfo_width());h=max(360,c.winfo_height())
        chunks=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',f'<rect width="{w}" height="{h}" fill="#07111f"/>']
        for item in c.find_all():
            typ=c.type(item);coords=c.coords(item);color=c.itemcget(item,'fill') or '#ffffff'
            if typ=='line' and len(coords)>=4:
                pts=' '.join(f'{coords[i]:.1f},{coords[i+1]:.1f}' for i in range(0,len(coords)-1,2))
                chunks.append(f'<polyline points="{pts}" fill="none" stroke="{html.escape(color)}" stroke-width="{c.itemcget(item,"width") or 1}"/>')
            elif typ=='text':
                label=html.escape(c.itemcget(item,'text'))
                chunks.append(f'<text x="{coords[0]:.1f}" y="{coords[1]+12:.1f}" fill="{html.escape(color)}" font-family="Consolas,monospace" font-size="12">{label}</text>')
            elif typ=='oval' and len(coords)==4:
                chunks.append(f'<ellipse cx="{(coords[0]+coords[2])/2:.1f}" cy="{(coords[1]+coords[3])/2:.1f}" rx="{(coords[2]-coords[0])/2:.1f}" ry="{(coords[3]-coords[1])/2:.1f}" fill="none" stroke="{html.escape(c.itemcget(item,"outline") or color)}"/>')
        chunks.append('</svg>')
        Path(path).write_text('\n'.join(chunks),encoding='utf-8')
        self.status.set('VISUAL DSP: SVG exported '+path)

    def visual_capture(self):
        self.visual_hold=[x.get() for x in self.vars]
        self.status.set('VISUAL DSP: comparison reference captured')

    def visual_clear(self):
        self.visual_hold=None
        self.status.set('VISUAL DSP: comparison cleared')

    def visual_export(self):
        from tkinter import filedialog
        import io
        path=filedialog.asksaveasfilename(defaultextension='.png',filetypes=[('PNG image','*.png')])
        if not path:return
        try:
            from PIL import Image
            ps=self.visual_canvas.postscript(colormode='color')
            im=Image.open(io.BytesIO(ps.encode('utf-8')))
            im.save(path,'PNG')
            self.status.set('VISUAL DSP: exported '+path)
        except Exception as exc:
            messagebox.showerror('PNG export','PNG export requires Pillow and Ghostscript.\n'+str(exc))

    def draw_visual(self,now):
        c=self.visual_canvas
        w=max(450,c.winfo_width());h=max(360,c.winfo_height())
        c.delete('all')
        def line(points,color,width=2):c.create_line(*points,fill=color,width=width,smooth=False)
        def label(x,y,t,color='#8eabc7',size=10):c.create_text(x,y,text=t,fill=color,font=('Consolas',size),anchor='nw')
        def grid(x0,y0,x1,y1):
            for i in range(9):
                x=x0+(x1-x0)*i/8;c.create_line(x,y0,x,y1,fill='#183048')
            for i in range(5):
                y=y0+(y1-y0)*i/4;c.create_line(x0,y,x1,y,fill='#183048')
        v=[x.get() for x in self.vars]
        if self.visual_morph_preview.get() and self.visual_a is not None and self.visual_b is not None:
            t=max(0,min(1,float(self.transport_position.get())/100))
            # Smoothstep follows the stored morph curve; discrete params switch at midpoint.
            if self.morph_vars[5].get():t=t*t*(3-2*t)
            v=[round(a+(b-a)*t) if i not in (9,11,12,14,20,21,22,26,27,29,35,38)
               else (a if t<.5 else b) for i,(a,b) in enumerate(zip(self.visual_a,self.visual_b))]
        dt=min(0.1,max(0,now-self.visual_last));self.visual_last=now
        # Display rate is an approximation: firmware LFO rate uses a nonlinear mapping.
        rate=0.04+5.0*(v[2]/127.0)**2
        self.visual_phase=(self.visual_phase+dt*rate)%1.0
        rate2=.04+5.0*(v[24]/127.0)**2
        self.visual_phase2=(self.visual_phase2+dt*rate2)%1.0
        dl,dr,l,r,lfo2=delays(v,self.visual_phase,self.visual_phase2)
        base=(dl+dr)/2
        top=(30,45,w-24,int(h*.53));bottom=(30,int(h*.65),w-24,h-25)
        if self.visual_show_grid.get():grid(*top);grid(*bottom)
        label(35,12,'STEREO COMB RESPONSE  |  DSP-INSPIRED MODEL','#cce9ff',12)
        x0,y0,x1,y1=top
        n=170 if self.visual_resolution.get()=='ECO' else 360
        freqs=[30*(20000/30)**(i/(n-1)) for i in range(n)]
        def points(db):
            return [coord for i,value in enumerate(db) for coord in (x0+(x1-x0)*i/(n-1),y1-(value+36)/54*(y1-y0))]
        left=response(v,dl,0,freqs,lfo2)
        right=response(v,dr,1,freqs,lfo2)
        if self.visual_hold and self.visual_show_hold.get():
            old=self.visual_hold
            odL,odR,_,_,ol2=delays(old,self.visual_phase,self.visual_phase2)
            line(points(response(old,odL,0,freqs,ol2)),'#436f83',1)
            line(points(response(old,odR,1,freqs,ol2)),'#86684e',1)
            label(x1-175,y0+7,'A/B REFERENCE','#a7b2c0',9)
        line(points(left),'#48d8ff',2);line(points(right),'#ffb869',2)
        if self.visual_stereo_difference.get():
            delta=[max(-36,min(18,lv-rv)) for lv,rv in zip(left,right)]
            line(points(delta),'#d28dff',1)
        if self.visual_peak_markers.get():
            for values,color in ((left,'#48d8ff'),(right,'#ffb869')):
                extrema=[(abs(values[i]-(values[i-4]+values[i+4])/2),i) for i in range(4,n-4)
                         if (values[i]>=values[i-1] and values[i]>=values[i+1]) or
                            (values[i]<=values[i-1] and values[i]<=values[i+1])]
                ranked=sorted(extrema,reverse=True)
                chosen=[]
                for _,idx in ranked:
                    if all(abs(idx-j)>10 for j in chosen):chosen.append(idx)
                    if len(chosen)>=3:break
                for idx in chosen:
                    px=x0+(x1-x0)*idx/(n-1);py=y1-(values[idx]+36)/54*(y1-y0)
                    c.create_oval(px-3,py-3,px+3,py+3,outline=color)
        label(x0+8,y0+7,'L  %.2f ms'%dl,'#48d8ff')
        label(x0+145,y0+7,'R  %.2f ms'%dr,'#ffb869')
        for freq in (100,1000,10000):
            xx=x0+(x1-x0)*math.log(freq/30)/math.log(20000/30)
            c.create_line(xx,y0,xx,y1,fill='#23405b')
            label(xx+3,y1-18,f'{freq//1000}k' if freq>=1000 else str(freq),size=8)
        label(35,int(h*.57),'LFO MODULATION  ·  STEREO PHASE' ,'#cce9ff',12)
        x0,y0,x1,y1=bottom;mid=(y0+y1)/2;ampl=(y1-y0)*.39
        for shift,color in ((0,'#48d8ff'),((v[18]+v[19])/254,'#ffb869')):
            pts=[]
            for i in range(250):
                t=self.visual_phase+i/249*2+shift
                pts.extend((x0+(x1-x0)*i/249,mid-wave(t,v[9])*ampl))
            line(pts,color,2)
        if self.visual_show_lfo2.get() and v[25]>0:
            pts=[]
            for i in range(250):
                t=(self.visual_phase2+i/249*2)%1.0
                w2=v[26]
                y2=math.sin(2*math.pi*t) if w2==0 else (4*abs(t-.5)-1 if w2==1 else (2*t-1 if w2==2 else (1 if t<.5 else -1)))
                pts.extend((x0+(x1-x0)*i/249,mid-y2*ampl*(v[25]/127.)))
            line(pts,'#9ed36a',1)
            label(x0+5,y0+4,'LFO2 (green)','#9ed36a',9)
        self.visual_info.set('TIME %.2f ms  |  FEEDBACK %d  |  MIX %d  |  LFO %.2f Hz (model)  |  MORPH %d%%  |  %s'%
            (base,v[1],v[4],rate,self.transport_position.get(),self.transport_state.get()))
    def reconnect_midi(self):
        """Reopen remembered ports on user request, without discarding current handles first."""
        try:
            in_name=self.inp.name;out_name=self.out.name
            new_in=mido.open_input(in_name)
            try:new_out=mido.open_output(out_name)
            except Exception:
                new_in.close();raise
        except Exception as exc:
            self.status.set(f'Reconnect failed: {exc}')
            messagebox.showwarning('MIDI reconnect',str(exc),parent=self.root)
            return
        old_in,old_out=self.inp,self.out
        self.inp,self.out=new_in,new_out
        for port in (old_in,old_out):
            try:port.close()
            except Exception:pass
        self.pending.clear()
        self._preset_snapshot_pending=None
        self.send(3);self.send(13)
        self.status.set('MIDI reconnected; requesting fresh parameter snapshot')

    def change_ports(self):
        # Stop current polling only after new ports were successfully opened.
        selection=choose_ports(self.root, self.inp.name, self.out.name)
        if not selection:return
        in_name,out_name=selection
        try:
            new_in=mido.open_input(in_name)
            try:new_out=mido.open_output(out_name)
            except Exception:
                new_in.close();raise
        except Exception as exc:
            messagebox.showerror('MIDI connection',str(exc),parent=self.root)
            return
        old_in,old_out=self.inp,self.out
        self.inp,self.out=new_in,new_out
        for port in (old_in,old_out):
            try:port.close()
            except Exception:pass
        self.status.set(f'MIDI connected: IN {in_name} | OUT {out_name}')
        save_midi_ports(in_name,out_name)
        self.send(3);self.send(13)

    def close(self):
        self._closing=True
        save_settings({'geometry':self.root.geometry(),'resolution':self.visual_resolution.get(),
                       'fps':self.visual_fps.get(),'grid':self.visual_show_grid.get()})
        try:self.inp.close()
        except Exception:pass
        try:self.out.close()
        except Exception:pass
        self.root.destroy()
def midi_config_path():
    return Path.home()/'.rtal_hq_flanger_midi_ports.json'

def save_midi_ports(in_name,out_name):
    """Persist only a pair of ports that has been opened successfully."""
    path=midi_config_path()
    temp=path.with_name(path.name+'.tmp')
    try:
        temp.write_text(json.dumps({'input':in_name,'output':out_name},indent=2),encoding='utf-8')
        temp.replace(path)
    except OSError:
        try:temp.unlink(missing_ok=True)
        except OSError:pass

def choose_ports(parent, initial_in=None, initial_out=None):
    """Modal GUI MIDI input/output selector, including refresh and remembered choices."""
    result=[None]
    dlg=tk.Toplevel(parent)
    dlg.title('RTAL HQ FLANGER — MIDI CONNECTION')
    dlg.transient(parent);dlg.grab_set();dlg.resizable(True,False)
    dlg.geometry('620x290')
    frame=ttk.Frame(dlg,padding=20);frame.pack(fill='both',expand=True)
    ttk.Label(frame,text='SELECT MIDI INPUT AND OUTPUT',style='Title.TLabel').pack(anchor='w',pady=(0,12))
    in_var=tk.StringVar(value=initial_in or '')
    out_var=tk.StringVar(value=initial_out or '')
    ttk.Label(frame,text='MIDI INPUT (Flanger → PC)').pack(anchor='w')
    ins=ttk.Combobox(frame,textvariable=in_var,state='readonly');ins.pack(fill='x',pady=(3,10))
    ttk.Label(frame,text='MIDI OUTPUT (PC → Flanger)').pack(anchor='w')
    outs=ttk.Combobox(frame,textvariable=out_var,state='readonly');outs.pack(fill='x',pady=(3,12))
    status=tk.StringVar(value='')
    def refresh():
        try:
            inputs=mido.get_input_names();outputs=mido.get_output_names()
            ins['values']=inputs;outs['values']=outputs
            if in_var.get() not in inputs:in_var.set(inputs[0] if len(inputs)==1 else '')
            if out_var.get() not in outputs:out_var.set(outputs[0] if len(outputs)==1 else '')
            status.set(f'{len(inputs)} input(s), {len(outputs)} output(s) found')
        except Exception as exc:status.set(str(exc))
    def connect():
        if not in_var.get() or not out_var.get():
            messagebox.showwarning('Select ports','Please select both MIDI ports.',parent=dlg);return
        result[0]=(in_var.get(),out_var.get());dlg.destroy()
    row=ttk.Frame(frame);row.pack(fill='x')
    ttk.Button(row,text='REFRESH PORTS',command=refresh).pack(side='left')
    ttk.Button(row,text='CANCEL',command=dlg.destroy).pack(side='right',padx=(8,0))
    ttk.Button(row,text='CONNECT',command=connect).pack(side='right')
    ttk.Label(frame,textvariable=status).pack(anchor='w',pady=(10,0))
    dlg.protocol('WM_DELETE_WINDOW',dlg.destroy)
    refresh();dlg.wait_window()
    return result[0]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--list',action='store_true');ap.add_argument('--input');ap.add_argument('--output')
    a=ap.parse_args()
    if a.list:
        print('INPUTS:',*mido.get_input_names(),sep='\n  ')
        print('OUTPUTS:',*mido.get_output_names(),sep='\n  ');return
    root=tk.Tk();root.withdraw()
    config=midi_config_path()
    remembered={}
    try:remembered=json.loads(config.read_text(encoding='utf-8'))
    except (OSError,ValueError,TypeError):pass
    if not isinstance(remembered,dict):remembered={}
    ports=None
    if a.input and a.output:
        ports=(a.input,a.output)
    else:
        # Phaser-style startup: silently reuse the last *successful* pair.
        in_name=remembered.get('input');out_name=remembered.get('output')
        if in_name and out_name:
            try:
                if in_name in mido.get_input_names() and out_name in mido.get_output_names():
                    ports=(in_name,out_name)
            except Exception:pass
        if ports is None:
            ports=choose_ports(root,in_name,out_name)
    if not ports:root.destroy();return
    try:
        app=App(root,*ports)
    except Exception as exc:
        # Previously available device may have been unplugged or busy.
        messagebox.showwarning('MIDI connection',
            f'Cannot open MIDI ports: {exc}\nPlease select available ports.',parent=root)
        ports=choose_ports(root,remembered.get('input'),remembered.get('output'))
        if not ports:root.destroy();return
        try:app=App(root,*ports)
        except Exception as second:
            messagebox.showerror('MIDI connection failed',str(second),parent=root)
            root.destroy();return
    save_midi_ports(*ports)
    root.deiconify();root.mainloop()
if __name__=='__main__':main()
