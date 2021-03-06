/* 
 * MobiDumper 
 * Utilities for content extraction and
 * markup "normalization" of mobi files
 * 
 * Author:  Domenico Rotiroti
 * License: GPL3 (see COPYING)
 */

#include "MobiDumper.h"
#include "JsonObj.h"
#include "Xml.h"
#include <iostream>
#include <stdio.h>
#include <algorithm>
#include <string>

using std::string;
using std::vector;

#define FPOSLEN 10

MobiDumper::~MobiDumper() {
}

void MobiDumper::dumpMetadata() {
    JsonObj meta;
    meta.add("author", book->getAuthor());
    meta.add("title", book->getTitle());
    meta.add("publisher", book->getPublisher());
    if(mobi->getCoverIndex() > 0)
        meta.add("cover", imgNames[mobi->getCoverIndex()]);
    vector<JsonObj> res;
    for(int i = 0; i < imgNames.size(); ++i) {
	JsonObj ares;
	ares.add("path", imgNames[i]);
	res.push_back(ares);
    }
    meta.add("items", txtFileNames);
    meta.add("res", res);

    JsonObj toc = buildToc();
    if(!toc.empty()) meta.add("toc", toc);
    
    write("info.json", meta.json());
}

string MobiDumper::fixLinks(string src) {
    char fbuf[24];

    // Step 1. fix a[@href]    
    string fmark = "filepos=", href="href=\"text_";
    size_t fml = fmark.length(), hl = href.length();
    string::size_type pos = src.find(fmark);
    while(pos!=string::npos) {
	src.replace(pos, fml, href);
	src.insert(pos+hl+FPOSLEN,".html\"");
	pos = src.find(fmark, pos+hl+FPOSLEN);
    }


    // Step 2. fix img[@src]
    for(int i = 0; i < imgNames.size(); ++i) {
	sprintf(fbuf, "recindex=\"%05d\"", i+1);
	string f1 = string(fbuf);
	string r1 = "src=\"";
	r1.append(imgNames[i]);
	r1.append("\"");
	replaceAll(src, f1, r1);
    }

    // Step 3. remove mobipocket's page break
    // (ok, it's not related to links, but it fits well here)
    replaceAll(src, "<mbp:pagebreak/>", "");
    return src;
}

#define CS_META "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />"
#define HTML_PROLOG "<html><head>" CS_META "</head><body>"
#define HTML_EPILOG "</body></html>"

void MobiDumper::dumpText() {
    char fbuf[24];
    string text = mobi->getText(), 
	    part;

    for( vector<int>::iterator ip = filepos.begin(); ip != filepos.end(); ip++) {
	//split at *ip
	part = fixLinks(text.substr(*ip));
	text = text.substr(0,*ip);
	//write part
	sprintf(fbuf, "text_%010d.html", *ip);
	txtFileNames.push_back(fbuf);
	write(fbuf, HTML_PROLOG+part+HTML_EPILOG);
    }

    text = fixLinks(text);
    size_t bpos = text.find("</head");
    if(bpos != string::npos) text.insert(bpos, CS_META);
    write("text.html", text+HTML_EPILOG);
    txtFileNames.push_back("text.html");
    std::reverse(txtFileNames.begin(), txtFileNames.end());
}

void MobiDumper::dumpResources() {
	ImageData * id;
	
	for(int i = 1; i <= mobi->imagesCount; ++i) {
	    id = mobi->getImage(i);
	    if(id==NULL) break;
	    
	    write(imgNames[i-1].c_str(), id->data, id->len);
	}
}

void MobiDumper::scanImages() {
	ImageData * id;
	char fname[PATHLEN];
	
	for(int i = 1; i <= mobi->imagesCount; ++i) {
	    id = mobi->getImage(i);
	    if(id==NULL) break;
	    sprintf(fname, "img_%03d%s", i, id->type);
	    imgNames.push_back(string(fname));
	}
}

void MobiDumper::scanLinks() {
    string txt = mobi->getText();
    string fmark = "filepos=";
    size_t fml = fmark.length();
    int val;
    
    // find all filepos marks (avoiding duplicates)
    string::size_type pos = txt.find(fmark);
    while(pos!=string::npos) {
	val = atoi(txt.substr(pos+fml, FPOSLEN).c_str());
	if( std::find(filepos.begin(), filepos.end(), val) == filepos.end())
		filepos.push_back(val);
	pos = txt.find(fmark, pos+fml);
    }
    
    // sort them in reverse order
    std::sort(filepos.begin(), filepos.end(), std::greater<int>());
}

JsonObj MobiDumper::buildToc() {
    JsonObj toc;
    char posstr[10];
    //dumpText should be called first!
    if(txtFileNames.size()==0) return toc;

    //look for toc reference in the first part
    Xml ref(read(txtFileNames[0]));
    Xpath rx = ref.xpath(NULL);
    string href = rx.get("//reference[@type='toc']/@href");
    if(href.empty()) return toc;
    //open toc file and process 'a' elements
    Xml tocx(read(href));
    Xpath tx = tocx.xpath(NULL);
    vector<string> links = tx.query("//a[@href]/@href");
    int pos;
    for(vector<string>::iterator it = links.begin(); it != links.end(); ++it) {
	JsonObj item;
	for(pos = txtFileNames.size()-1; pos >= 0; --pos) {
	    if(txtFileNames[pos]==*it) break;
	}
	snprintf(posstr, 9, "%d", pos);
	item.add("pos", posstr);
	string q("//a[@href='");
	q.append(*it).append("']");
	item.add("name", tx.get(q));
	//item.add("name", *it); //should be <a>.value
	toc.add(*it, item);
    }
    
    return toc;
}