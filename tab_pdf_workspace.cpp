// tab_pdf_workspace.cpp
// Professional PDF Workspace - SumatraPDF Style with Multi-Tab Support

#define _CRT_SECURE_NO_WARNINGS
#include "tab_pdf_workspace.h"
#include "mini_browser.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <Shlwapi.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/event.h>
#include <commdlg.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Comdlg32.lib")

using namespace Gdiplus;
using namespace Microsoft::WRL;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;
extern wstring currentWorkspacePdf;

// WebView2 Global
HWND g_hAcrobatWnd = NULL;
HWND g_hWebViewWnd = NULL;
ComPtr<ICoreWebView2Environment> g_webViewEnv = nullptr;
ComPtr<ICoreWebView2Controller> g_webViewController = nullptr;
ComPtr<ICoreWebView2> g_webView = nullptr;
wstring g_acrobatPdfPath = L"";
bool g_webViewInitialized = false;

HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd);
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

// ==========================================
// HTML - COMPLETE PROFESSIONAL PDF VIEWER
// ==========================================
wstring GetAcrobatHTML() {
    wstring h = LR"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>PDF Pro</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet"/>
<style>
:root{--red:#EB1C24;--rh:#BA1617;--dk:#2C2C2C;--pn:#F0F0F0;--doc:#E8E8E8;--bd:#D0D0D0;--tx:#222;--mu:#777;--hv:#E5E5E5;--th:44px;--tbh:38px;--lsw:200px;--rsw:240px}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;height:100vh;overflow:hidden;display:flex;flex-direction:column;background:var(--doc);color:var(--tx);user-select:none}
.ms{font-variation-settings:'FILL'0,'wght'400,'GRAD'0,'opsz'24;font-size:20px}
.toast-container{position:fixed;bottom:30px;left:50%;transform:translateX(-50%);z-index:9999;display:flex;flex-direction:column;gap:8px}
.toast{padding:10px 24px;border-radius:4px;color:#fff;background:#323232;font-size:13px;box-shadow:0 4px 12px rgba(0,0,0,0.2);animation:fu 0.3s}
@keyframes fu{from{transform:translateY(20px);opacity:0}to{transform:translateY(0);opacity:1}}
.modal-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.5);z-index:1000;justify-content:center;align-items:center}
.modal-overlay.show{display:flex}
.modal{background:#fff;border-radius:6px;padding:20px;min-width:340px;box-shadow:0 8px 24px rgba(0,0,0,0.2)}
.modal h3{font-size:17px;margin-bottom:14px;font-weight:600}
.modal input{width:100%;padding:8px;margin-bottom:14px;border:1px solid var(--bd);border-radius:4px}
.modal-actions{display:flex;gap:8px;justify-content:flex-end}
.btn{padding:7px 14px;border:none;border-radius:4px;cursor:pointer;font-size:13px;font-weight:500;transition:0.2s}
.btn-primary{background:var(--red);color:#fff}.btn-primary:hover{background:var(--rh)}
.btn-secondary{background:transparent;border:1px solid var(--bd);color:var(--tx)}.btn-secondary:hover{background:var(--hv)}
.topbar{height:var(--th);background:var(--dk);display:flex;align-items:center;padding:0 12px;color:#fff;gap:8px}
.topbar-menu{display:flex;gap:16px;font-size:12px}
.topbar-item{cursor:pointer;opacity:0.8;transition:0.2s}.topbar-item:hover{opacity:1}
.topbar-title{flex:1;text-align:center;font-size:13px;font-weight:500;opacity:0.9;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.topbar-actions{display:flex;gap:10px}
.topbar-icon{cursor:pointer;opacity:0.8}.topbar-icon:hover{opacity:1}
.tab-bar{height:34px;background:#444;display:flex;align-items:center;padding:0 4px;gap:2px;overflow-x:auto;overflow-y:hidden}
.tab-item{display:flex;align-items:center;gap:6px;padding:4px 10px;background:#555;color:#ccc;font-size:11px;border-radius:4px 4px 0 0;cursor:pointer;white-space:nowrap;min-width:80px;max-width:180px;transition:0.2s}
.tab-item.active{background:#666;color:#fff}
.tab-item:hover{background:#6a6a6a}
.tab-close{margin-left:auto;font-size:14px;opacity:0.6;cursor:pointer;line-height:1}.tab-close:hover{opacity:1;color:#f44}
.tab-add{display:flex;align-items:center;justify-content:center;width:28px;height:28px;border-radius:4px;cursor:pointer;color:#aaa;font-size:18px;transition:0.2s;flex-shrink:0}.tab-add:hover{background:#555;color:#fff}
.toolbar{height:var(--tbh);background:#fff;border-bottom:1px solid var(--bd);display:flex;align-items:center;padding:0 10px;gap:12px;font-size:13px}
.tool-btn{display:flex;align-items:center;gap:4px;cursor:pointer;padding:4px 8px;border-radius:4px;color:var(--tx);transition:0.2s}
.tool-btn:hover{background:var(--hv)}.tool-btn.active{background:#FBECEE;color:var(--red)}
.divider{width:1px;height:18px;background:var(--bd)}
.workspace{flex:1;display:flex;overflow:hidden}
.left-sidebar{width:var(--lsw);background:var(--pn);border-right:1px solid var(--bd);display:flex;flex-direction:column;transition:0.3s}
.left-sidebar.hidden{width:0!important;border:none!important;overflow:hidden}
.sidebar-header{display:flex;align-items:center;justify-content:space-between;padding:10px 12px;font-size:11px;font-weight:600;text-transform:uppercase;color:var(--mu);border-bottom:1px solid var(--bd)}
.sidebar-toggle{cursor:pointer;opacity:0.6;font-size:16px}.sidebar-toggle:hover{opacity:1}
.thumb-list{flex:1;overflow-y:auto;padding:8px;display:flex;flex-direction:column;gap:8px}
.thumb-item{border:1px solid var(--bd);background:#fff;padding:3px;cursor:pointer;transition:0.2s}
.thumb-item:hover{border-color:var(--red)}
.thumb-item.active{border:2px solid var(--red)}
.thumb-item canvas{width:100%;height:auto;display:block}
.thumb-label{text-align:center;font-size:10px;margin-top:2px;color:var(--mu)}
.right-sidebar{width:var(--rsw);background:var(--pn);border-left:1px solid var(--bd);display:flex;flex-direction:column;overflow-y:auto;transition:0.3s}
.right-sidebar.hidden{width:0!important;border:none!important;overflow:hidden}
.rs-header{display:flex;align-items:center;justify-content:space-between;padding:10px 12px;font-size:11px;font-weight:600;text-transform:uppercase;color:var(--mu);border-bottom:1px solid var(--bd)}
.tool-pane-btn{display:flex;align-items:center;gap:8px;padding:10px 12px;cursor:pointer;border-bottom:1px solid var(--bd);background:#fff;transition:0.2s}
.tool-pane-btn:hover{background:var(--hv)}.tool-pane-btn .ms{color:var(--red);font-size:22px}
.tool-pane-text{font-size:13px;font-weight:500}
.pdf-viewer-area{flex:1;overflow-y:auto;display:flex;justify-content:center;padding:20px;background:var(--doc)}
.pdf-container{display:flex;flex-direction:column;gap:16px;align-items:center;width:100%;cursor:default}
.pdf-page-wrapper{background:#fff;box-shadow:0 2px 8px rgba(0,0,0,0.12);margin-bottom:16px;position:relative}
.pdf-page-wrapper canvas{display:block;max-width:100%;height:auto}
.loading-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.6);z-index:2000;flex-direction:column;justify-content:center;align-items:center;color:#fff;font-size:15px}
.loading-overlay.show{display:flex}
.spin{border:4px solid rgba(255,255,255,0.3);border-top:4px solid #fff;border-radius:50%;width:36px;height:36px;animation:spin 1s linear infinite;margin-bottom:12px}
@keyframes spin{0%{transform:rotate(0)}100%{transform:rotate(360deg)}}
body.night .pdf-viewer-area{background:#1a1a1a!important}
body.night .toolbar{background:#252525!important;border-color:#333!important;color:#e0e0e0!important}
body.night .tool-btn{color:#e0e0e0!important}.night .tool-btn:hover{background:#333!important}
body.night .divider{background:#333!important}
body.night .left-sidebar,body.night .right-sidebar{background:#252525!important;border-color:#333!important}
body.night .sidebar-header,body.night .rs-header{color:#aaa!important;border-color:#333!important}
body.night .thumb-item{background:#1a1a1a!important;border-color:#333!important}
body.night .tool-pane-btn{background:#252525!important;border-color:#333!important;color:#e0e0e0!important}
body.night .tool-pane-btn:hover{background:#333!important}
body.night .pdf-page-wrapper{box-shadow:0 4px 15px rgba(0,0,0,0.5)}
body.read .toolbar,body.read .left-sidebar,body.read .right-sidebar{display:none!important}
body.read .tab-bar{display:none!important}
</style></head><body>
<div class="toast-container" id="tc"></div>
<div class="loading-overlay" id="lo"><div class="spin"></div><div id="lt">Processing...</div></div>
<div class="modal-overlay" id="mo"><div class="modal" id="mc"></div></div>
<div class="topbar">
<div class="topbar-menu"><div class="topbar-item" onclick="of()">Open</div><div class="topbar-item" onclick="sf()">Save</div><div class="topbar-item" onclick="saf()">Save As</div></div>
<div class="topbar-title" id="at">PDF Pro</div>
<div class="topbar-actions">
<span class="ms topbar-icon" onclick="tn()" title="Night Mode">dark_mode</span>
<span class="ms topbar-icon" onclick="tr()" title="Read Mode" id="ri">menu_book</span>
<span class="ms topbar-icon" onclick="tl()" id="lti" title="Toggle Left">dock_to_left</span>
<span class="ms topbar-icon" onclick="trt()" id="rti" title="Toggle Right">dock_to_right</span>
</div></div>
<div class="tab-bar" id="tb"></div>
<div class="toolbar">
<div class="tool-btn active" onclick="ss(null)" id="tp"><span class="ms">pan_tool</span></div>
<div class="divider"></div>
<span style="font-weight:600;font-size:11px;color:var(--mu)">TOOLS:</span>
<div class="tool-btn" onclick="ss('hl')" id="thl"><span class="ms" style="color:#FFC107">format_ink_highlighter</span></div>
<div class="tool-btn" onclick="ss('nt')" id="tnt"><span class="ms" style="color:#4CAF50">speaker_notes</span></div>
<div class="tool-btn" onclick="ss('lk')" id="tlk"><span class="ms" style="color:#2196F3">link</span></div>
<div class="divider"></div>
<div class="tool-btn" onclick="zo()"><span class="ms">remove</span></div>
<span id="zt" style="font-size:12px;width:36px;text-align:center">100%</span>
<div class="tool-btn" onclick="zi()"><span class="ms">add</span></div>
<div class="divider"></div>
<div class="tool-btn" onclick="rp()"><span class="ms">rotate_right</span></div>
</div>
<div class="workspace">
<div class="left-sidebar" id="ls">
<div class="sidebar-header"><span>Pages</span><span class="ms sidebar-toggle" onclick="tl()">chevron_left</span></div>
<div class="thumb-list" id="tlst"></div>
</div>
<div class="pdf-viewer-area" id="va"><div class="pdf-container" id="pc">
<div style="margin-top:80px;text-align:center;color:var(--mu)">
<span class="ms" style="font-size:60px;color:#ccc">description</span>
<p style="margin-top:12px;font-size:15px">Open PDF or drag here</p>
<button class="btn btn-primary" style="margin-top:12px" onclick="of()">Open File</button>
</div></div></div>
<div class="right-sidebar" id="rs">
<div class="rs-header"><span>Tools</span><span class="ms sidebar-toggle" onclick="trt()">chevron_right</span></div>
<div class="tool-pane-btn" onclick="sm()"><span class="ms">library_add</span><span class="tool-pane-text">Merge</span></div>
<div class="tool-pane-btn" onclick="ssp()"><span class="ms">splitscreen</span><span class="tool-pane-text">Split</span></div>
<div class="tool-pane-btn" onclick="sep()"><span class="ms">file_upload</span><span class="tool-pane-text">Extract</span></div>
<div class="tool-pane-btn" onclick="sdp()"><span class="ms">delete</span><span class="tool-pane-text">Delete</span></div>
<div class="tool-pane-btn" onclick="pi()"><span class="ms">image</span><span class="tool-pane-text">To Images</span></div>
<div class="tool-pane-btn" onclick="pt()"><span class="ms">article</span><span class="tool-pane-text">To Text</span></div>
<div class="tool-pane-btn" onclick="ocr()"><span class="ms">document_scanner</span><span class="tool-pane-text">OCR</span></div>
<div class="tool-pane-btn" onclick="aw()"><span class="ms">branding_watermark</span><span class="tool-pane-text">Watermark</span></div>
<div class="tool-pane-btn" onclick="as()"><span class="ms">verified</span><span class="tool-pane-text">Stamp</span></div>
</div></div>
<input type="file" id="fi" accept=".pdf" style="display:none" onchange="ho(event)">
<input type="file" id="mfi" accept=".pdf" multiple style="display:none">
<script>
let tabs=[],activeTab=-1,cz=1.0,cr=0,st=null,lv=true,rv=true;
function sts(m){let c=document.getElementById('tc'),t=document.createElement('div');t.className='toast';t.textContent=m;c.appendChild(t);setTimeout(()=>t.remove(),2500)}
function sl(s,t="Working..."){document.getElementById('lo').classList.toggle('show',s);document.getElementById('lt').textContent=t}
function smd(t,h){document.getElementById('mc').innerHTML=`<h3>${t}</h3>${h}`;document.getElementById('mo').classList.add('show')}
function cmd(){document.getElementById('mo').classList.remove('show')}
function of(){document.getElementById('fi').click()}
function sf(){if(activeTab>=0&&tabs[activeTab])db(tabs[activeTab].bytes,tabs[activeTab].name)}
function saf(){if(activeTab>=0&&tabs[activeTab]){let n=prompt('Save as:',tabs[activeTab].name);if(n)db(tabs[activeTab].bytes,n)}}
function db(b,n){let bl=new Blob([b],{type:'application/pdf'});saveAs(bl,n);sts('Saved: '+n)}
function tl(){lv=!lv;document.getElementById('ls').classList.toggle('hidden',!lv);document.getElementById('lti').style.color=lv?'':'var(--red)';sts(lv?'Sidebar shown':'Sidebar hidden')}
function trt(){rv=!rv;document.getElementById('rs').classList.toggle('hidden',!rv);document.getElementById('rti').style.color=rv?'':'var(--red)';sts(rv?'Tools shown':'Tools hidden')}
function tn(){document.body.classList.toggle('night');sts(document.body.classList.contains('night')?'Night Mode':'Light Mode')}
function tr(){document.body.classList.toggle('read');let r=document.body.classList.contains('read'),i=document.getElementById('ri');i.textContent=r?'fullscreen_exit':'menu_book';i.style.color=r?'var(--red)':'';if(r&&cz<1.5)cz=1.5;if(!r)cz=1.0;setTimeout(rv,100);sts(r?'Read Mode':'Normal Mode')}
function atab(t){activeTab=t;rv();rth();sts('Tab '+(t+1))}
function ctab(t){tabs.splice(t,1);if(activeTab>=tabs.length)activeTab=tabs.length-1;rtb();rv();rth()}
function ntab(){of()}
function rtb(){let b=document.getElementById('tb');b.innerHTML='';tabs.forEach((t,i)=>{let d=document.createElement('div');d.className='tab-item'+(i===activeTab?' active':'');d.innerHTML=`<span>${t.name}</span><span class="tab-close" onclick="event.stopPropagation();ctab(${i})">&times;</span>`;d.onclick=()=>atab(i);b.appendChild(d)});let a=document.createElement('div');a.className='tab-add';a.innerHTML='+';a.onclick=ntab;b.appendChild(a)}
function ho(e){let f=e.target.files[0];if(!f)return;let r=new FileReader();r.onload=async function(){let u=new Uint8Array(r.result);tabs.push({name:f.name,bytes:u,doc:null});activeTab=tabs.length-1;rtb();await lp(u);sts('Loaded: '+f.name)};r.readAsArrayBuffer(f)}
async function lpp(p){try{let r=await fetch('file:///'+p.replace(/\\/g,'/'));let ab=await r.arrayBuffer();let n=p.split('\\').pop();tabs.push({name:n,bytes:new Uint8Array(ab),doc:null});activeTab=tabs.length-1;rtb();await lp(new Uint8Array(ab))}catch(e){sts('Load failed')}}
async function lp(u){try{tabs[activeTab].bytes=u;tabs[activeTab].doc=await pdfjsLib.getDocument({data:u}).promise;cz=1.0;cr=0;document.getElementById('at').textContent=tabs[activeTab].name;await rv();await rth();sts('Ready')}catch(e){sts('Error loading PDF')}}
async function rv(){let d=tabs[activeTab]?tabs[activeTab].doc:null;let c=document.getElementById('pc');c.innerHTML='';if(!d){c.innerHTML=`<div style="margin-top:80px;text-align:center;color:var(--mu)"><span class="ms" style="font-size:60px;color:#ccc">description</span><p style="margin-top:12px;font-size:15px">No PDF open</p></div>`;return}document.getElementById('zt').textContent=Math.round(cz*100)+'%';for(let i=1;i<=d.numPages;i++){let p=await d.getPage(i);let v=p.getViewport({scale:cz,rotation:cr});let w=document.createElement('div');w.className='pdf-page-wrapper';w.id='pg-'+i;let cv=document.createElement('canvas');cv.height=v.height;cv.width=v.width;w.appendChild(cv);c.appendChild(w);await p.render({canvasContext:cv.getContext('2d'),viewport:v}).promise}}
async function rth(){let d=tabs[activeTab]?tabs[activeTab].doc:null;let l=document.getElementById('tlst');l.innerHTML='';if(!d)return;for(let i=1;i<=d.numPages;i++){let p=await d.getPage(i);let v=p.getViewport({scale:0.18,rotation:cr});let it=document.createElement('div');it.className='thumb-item';it.onclick=()=>document.getElementById('pg-'+i).scrollIntoView({behavior:'smooth'});let cv=document.createElement('canvas');cv.height=v.height;cv.width=v.width;let lb=document.createElement('div');lb.className='thumb-label';lb.textContent=i;it.appendChild(cv);it.appendChild(lb);l.appendChild(it);await p.render({canvasContext:cv.getContext('2d'),viewport:v}).promise}}
function zi(){if(cz<3.0){cz+=0.2;rv()}}
function zo(){if(cz>0.4){cz-=0.2;rv()}}
function rp(){cr=(cr+90)%360;rv();rth()}
function ss(t){st=t;document.querySelectorAll('.toolbar .tool-btn').forEach(b=>b.classList.remove('active'));if(t==='hl')document.getElementById('thl').classList.add('active');else if(t==='nt')document.getElementById('tnt').classList.add('active');else if(t==='lk')document.getElementById('tlk').classList.add('active');else{document.getElementById('tp').classList.add('active');st=null}document.getElementById('pc').style.cursor=st?'crosshair':'default';sts(st?'Tool: '+st.toUpperCase():'Normal mode')}
document.getElementById('pc').addEventListener('click',async(e)=>{if(!st||activeTab<0||!tabs[activeTab])return;let w=e.target.closest('.pdf-page-wrapper');if(!w)return;let pi=parseInt(w.id.split('-')[1])-1;let r=e.target.getBoundingClientRect();let cx=e.clientX-r.left,cy=e.clientY-r.top;sl(true,'Applying '+st+'...');try{let d=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let p=d.getPages()[pi];let{width,height}=p.getSize();let px=(cx/r.width)*width,py=height-((cy/r.height)*height);if(st==='hl'){p.drawRectangle({x:px,y:py-5,width:120,height:15,color:PDFLib.rgb(1,1,0),opacity:0.4,blendMode:PDFLib.BlendMode.Multiply})}else if(st==='nt'){let n=prompt('Note:');if(n){p.drawRectangle({x:px,y:py-30,width:200,height:40,color:PDFLib.rgb(0.98,0.96,0.84),borderColor:PDFLib.rgb(0.8,0.6,0.2),borderWidth:1});p.drawText('📝 '+n,{x:px+5,y:py-15,size:12,color:PDFLib.rgb(0,0,0)})}}else if(st==='lk'){let u=prompt('URL:');if(u){p.drawText('🔗 '+u,{x:px,y:py,size:10,color:PDFLib.rgb(0,0,1)});let a=d.context.obj({Type:'Annot',Subtype:'Link',Rect:[px,py-5,px+150,py+10],Border:[0,0,0],A:{Type:'Action',S:'URI',URI:PDFLib.PDFString.of(u)}});let ar=d.context.register(a);let an=p.node.Annots();if(!an){an=d.context.obj([]);p.node.set(PDFLib.PDFName.of('Annots'),an)}an.push(ar)}}tabs[activeTab].bytes=await d.save();await lp(tabs[activeTab].bytes)}catch(e){sts('Error')}sl(false)})
async function aw(){if(activeTab<0)return sts('Open PDF');let t=prompt('Watermark:','DRAFT');if(!t)return;sl(true,'Adding...');try{let d=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let{rgb,degrees}=PDFLib;d.getPages().forEach(p=>{let{w,h}=p.getSize();p.drawText(t,{x:w/2-150,y:h/2,size:60,color:rgb(0.9,0.2,0.2),opacity:0.3,rotate:degrees(45)})});tabs[activeTab].bytes=await d.save();await lp(tabs[activeTab].bytes);sts('Done')}catch(e){sts('Failed')}sl(false)}
async function as(){if(activeTab<0)return sts('Open PDF');sl(true,'Stamping...');try{let d=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let p=d.getPages()[0];let{w,h}=p.getSize();let{rgb}=PDFLib;p.drawRectangle({x:w-220,y:h-100,width:180,height:50,borderColor:rgb(0.1,0.6,0.1),borderWidth:3});p.drawText('APPROVED',{x:w-200,y:h-85,size:30,color:rgb(0.1,0.6,0.1)});tabs[activeTab].bytes=await d.save();await lp(tabs[activeTab].bytes);sts('Done')}catch(e){sts('Failed')}sl(false)}
function sm(){document.getElementById('mfi').click();document.getElementById('mfi').onchange=async function(e){let fs=e.target.files;if(fs.length<2)return alert('Select 2+ files');cmd();sl(true,'Merging...');try{let md=await PDFLib.PDFDocument.create();for(let f of fs){let pd=await PDFLib.PDFDocument.load(new Uint8Array(await f.arrayBuffer()));let cp=await md.copyPages(pd,pd.getPageIndices());cp.forEach(p=>md.addPage(p))}let mb=await md.save();tabs.push({name:'Merged.pdf',bytes:mb,doc:null});activeTab=tabs.length-1;rtb();await lp(mb);db(mb,'Merged.pdf')}catch(e){sts('Merge failed')}sl(false)}}
function ssp(){if(activeTab<0)return sts('Open PDF');smd('Split','<p>After page:</p><input type="number" id="spp" min="1" max="'+tabs[activeTab].doc.numPages+'" value="1"><div class="modal-actions"><button class="btn btn-secondary" onclick="cmd()">Cancel</button><button class="btn btn-primary" onclick="asp()">Split</button></div>')}
async function asp(){let n=parseInt(document.getElementById('spp').value);cmd();sl(true,'Splitting...');try{let s=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let d1=await PDFLib.PDFDocument.create(),d2=await PDFLib.PDFDocument.create();let idx=s.getPageIndices();let c1=await d1.copyPages(s,idx.slice(0,n));c1.forEach(p=>d1.addPage(p));let c2=await d2.copyPages(s,idx.slice(n));c2.forEach(p=>d2.addPage(p));db(await d1.save(),'Part1.pdf');db(await d2.save(),'Part2.pdf')}catch(e){sts('Failed')}sl(false)}
function sep(){if(activeTab<0)return sts('Open PDF');smd('Extract','<p>Pages (comma):</p><input type="text" id="epi" placeholder="1,2"><div class="modal-actions"><button class="btn btn-secondary" onclick="cmd()">Cancel</button><button class="btn btn-primary" onclick="aep()">Extract</button></div>')}
async function aep(){let inp=document.getElementById('epi').value;let pg=inp.split(',').map(n=>parseInt(n.trim())-1).filter(n=>!isNaN(n)&&n>=0);if(!pg.length)return alert('Invalid');cmd();sl(true,'Extracting...');try{let s=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let nd=await PDFLib.PDFDocument.create();let cp=await nd.copyPages(s,pg);cp.forEach(p=>nd.addPage(p));let eb=await nd.save();tabs.push({name:'Extracted.pdf',bytes:eb,doc:null});activeTab=tabs.length-1;rtb();await lp(eb)}catch(e){sts('Failed')}sl(false)}
function sdp(){if(activeTab<0)return sts('Open PDF');smd('Delete','<p>Page:</p><input type="number" id="dpi" min="1" max="'+tabs[activeTab].doc.numPages+'"><div class="modal-actions"><button class="btn btn-secondary" onclick="cmd()">Cancel</button><button class="btn btn-primary" style="background:#D13438" onclick="adp()">Delete</button></div>')}
async function adp(){let n=parseInt(document.getElementById('dpi').value)-1;cmd();sl(true,'Deleting...');try{let s=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);s.removePage(n);tabs[activeTab].bytes=await s.save();await lp(tabs[activeTab].bytes)}catch(e){sts('Failed')}sl(false)}
async function pi(){if(activeTab<0)return sts('Open PDF');sl(true,'Converting...');try{let z=new JSZip();let d=tabs[activeTab].doc;for(let i=1;i<=d.numPages;i++){let p=await d.getPage(i);let v=p.getViewport({scale:2.0});let c=document.createElement('canvas');c.height=v.height;c.width=v.width;await p.render({canvasContext:c.getContext('2d'),viewport:v}).promise;let b=await new Promise(r=>c.toBlob(r,'image/png'));z.file('Page_'+i+'.png',b)}let zb=await z.generateAsync({type:'blob'});saveAs(zb,'Images.zip');sts('Done')}catch(e){sts('Failed')}sl(false)}
async function pt(){if(activeTab<0)return sts('Open PDF');sl(true,'Extracting...');try{let t='';let d=tabs[activeTab].doc;for(let i=1;i<=d.numPages;i++){let p=await d.getPage(i);let tc=await p.getTextContent();t+='--- Page '+i+' ---\n\n'+tc.items.map(it=>it.str).join(' ')+'\n\n'}saveAs(new Blob([t],{type:'text/plain'}),'Text.txt');sts('Done')}catch(e){sts('Failed')}sl(false)}
async function ocr(){if(activeTab<0)return sts('Open PDF');sl(true,'OCR...');try{let d=tabs[activeTab].doc;let p=await d.getPage(1);let v=p.getViewport({scale:2.0});let c=document.createElement('canvas');c.height=v.height;c.width=v.width;await p.render({canvasContext:c.getContext('2d'),viewport:v}).promise;let r=await Tesseract.recognize(c.toDataURL('image/png'),'eng');saveAs(new Blob([r.data.text],{type:'text/plain'}),'OCR.txt');sts('Done')}catch(e){sts('Failed')}sl(false)}
document.addEventListener('wheel',(e)=>{if(e.ctrlKey){e.preventDefault();if(e.deltaY<0)zi();else zo()}},{passive:false})
document.addEventListener('paste',async(e)=>{if(activeTab<0)return;let it=e.clipboardData.items,im=null;for(let i=0;i<it.length;i++){if(it[i].type.indexOf('image')!==-1){im=it[i].getAsFile();break}}if(!im)return;sl(true,'Pasting...');try{let ib=await im.arrayBuffer();let d=await PDFLib.PDFDocument.load(tabs[activeTab].bytes);let p=d.getPages()[0],ei;if(im.type==='image/png')ei=await d.embedPng(ib);else if(im.type==='image/jpeg'||im.type==='image/jpg')ei=await d.embedJpg(ib);else{sts('PNG/JPG only');sl(false);return}let{w,h}=p.getSize();let dim=ei.scale(0.5);p.drawImage(ei,{x:w/2-dim.width/2,y:h/2-dim.height/2,width:dim.width,height:dim.height});tabs[activeTab].bytes=await d.save();await lp(tabs[activeTab].bytes);sts('Pasted')}catch(e){sts('Paste failed')}sl(false)})
</script></body></html>
)HTML";
    return h;
}

// ==========================================
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r; GetClientRect(hWnd, &r);
        g_hWebViewWnd = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS, 0,0,r.right,r.bottom, hWnd,(HMENU)1001,GetModuleHandle(NULL),NULL);
        InitializeWebView2(hWnd, g_hWebViewWnd);
        break;
    }
    case WM_SIZE: {
        if(g_hWebViewWnd && g_webViewController) {
            RECT r; GetClientRect(hWnd, &r);
            SetWindowPos(g_hWebViewWnd,NULL,0,0,r.right,r.bottom,SWP_NOZORDER);
            g_webViewController->put_Bounds(RECT{0,0,r.right,r.bottom});
        }
        break;
    }
    case WM_CLOSE: { ShowWindow(hWnd, SW_HIDE); return 0; }
    case WM_DESTROY: {
        if(g_webViewController){g_webViewController->Close();g_webViewController=nullptr;}
        g_webView=nullptr;g_webViewEnv=nullptr;g_webViewInitialized=false;
        if(g_hWebViewWnd){DestroyWindow(g_hWebViewWnd);g_hWebViewWnd=NULL;}
        g_hAcrobatWnd=NULL;
        break;
    }
    default: return DefWindowProcW(hWnd,msg,wp,lp);
    }
    return 0;
}

// ==========================================
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd) {
    auto ec = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hWnd,hHostWnd](HRESULT r, ICoreWebView2Environment* env) -> HRESULT {
            if(FAILED(r)) return r;
            g_webViewEnv = env;
            auto cc = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [hWnd](HRESULT r, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if(FAILED(r)) return r;
                    g_webViewController = ctrl;
                    g_webViewController->get_CoreWebView2(&g_webView);
                    ICoreWebView2Settings* s;
                    g_webView->get_Settings(&s);
                    s->put_IsScriptEnabled(TRUE);
                    s->put_IsWebMessageEnabled(TRUE);
                    RECT rc; GetClientRect(hWnd,&rc);
                    g_webViewController->put_Bounds(RECT{0,0,rc.right,rc.bottom});
                    g_webView->NavigateToString(GetAcrobatHTML().c_str());
                    auto nc = Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                            BOOL ok; args->get_IsSuccess(&ok);
                            if(ok){g_webViewInitialized=true;
                                if(!g_acrobatPdfPath.empty()){
                                    wstring ep=g_acrobatPdfPath;size_t p=0;
                                    while((p=ep.find(L"\\",p))!=wstring::npos){ep.replace(p,1,L"\\\\");p+=2;}
                                    wstring sc=L"lpp('"+ep+L"');";
                                    sender->ExecuteScript(sc.c_str(),nullptr);
                                }}
                            return S_OK;
                        });
                    g_webView->add_NavigationCompleted(nc.Get(),nullptr);
                    return S_OK;
                });
            env->CreateCoreWebView2Controller(hHostWnd,cc.Get());
            return S_OK;
        });
    return CreateCoreWebView2EnvironmentWithOptions(nullptr,nullptr,nullptr,ec.Get());
}

// ==========================================
void LaunchFoxitStylePdfReader(wstring pdfPath) {
    g_acrobatPdfPath = pdfPath;
    if(g_hAcrobatWnd){
        ShowWindow(g_hAcrobatWnd,SW_RESTORE);SetForegroundWindow(g_hAcrobatWnd);
        if(g_webViewInitialized&&g_webView&&!pdfPath.empty()){
            wstring ep=pdfPath;size_t p=0;
            while((p=ep.find(L"\\",p))!=wstring::npos){ep.replace(p,1,L"\\\\");p+=2;}
            wstring sc=L"lpp('"+ep+L"');";
            g_webView->ExecuteScript(sc.c_str(),nullptr);
        }
        return;
    }
    static bool reg=false;
    if(!reg){
        WNDCLASSW wc={0};wc.lpfnWndProc=AcrobatViewerWndProc;wc.hInstance=GetModuleHandle(NULL);
        wc.lpszClassName=L"PDFProClass";wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);reg=true;
    }
    g_hAcrobatWnd = CreateWindowExW(0,L"PDFProClass",L"PDF Pro",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,(int)(1200*g_scaleFactor),(int)(800*g_scaleFactor),
        NULL,NULL,GetModuleHandle(NULL),NULL);
    ShowWindow(g_hAcrobatWnd,SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_hAcrobatWnd);UpdateWindow(g_hAcrobatWnd);
}

void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font ft(&ff,20*g_scaleFactor,FontStyleBold,UnitPixel);
    SolidBrush tb(Color(255,100,100,100));
    StringFormat sf;sf.SetAlignment(StringAlignmentCenter);sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Pro Ready",-1,&ft,RectF(cx,cy,cw,ch),&sf,&tb);
}
void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
