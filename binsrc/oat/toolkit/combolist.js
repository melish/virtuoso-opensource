/*
 *  $Id$
 *
 *  This file is part of the OpenLink Software Ajax Toolkit (OAT) project.
 *
 *  Copyright (C) 2006 Ondrej Zara and OpenLink Software
 *
 *  See LICENSE file for details.
 */
/*
	var cl = new OAT.Combolist(optList,value)
	cl.onchange = callback;
	appendChild(cl.div)
	
	cl.clearOpts()
	cl.addOption(name, value)
	
	CSS: combo_list, combo_list_input, combo_list_option, combo_list_list
*/

OAT.Combolist = function(optList,value,optObj) {
	var self = this;
	
	this.options = {
		name:"combo_list", /* name of input element */
		imagePath:OAT.Preferences.imagePath
	}
	
	for (var p in optObj) { self.options[p] = optObj[p]; }
	
	this.value = value;
	this.div = OAT.Dom.create("div",{},"combo_list");
	this.onchange = function() {};
	
	this.img = OAT.Dom.create("img");
	this.img.src = self.options.imagePath + "Combolist_select.gif";
	this.input = OAT.Dom.create("input",{},"combo_list_input");
	this.input.type = "text";
	this.input.name = self.options.name;
	this.input.value = value;
	
	this.list = OAT.Dom.create("div",{position:"absolute",left:"0px",top:"0px",zIndex:200},"combo_list_list");
	OAT.Dom.attach(this.input,"keyup",function(){self.value = self.input.value; self.onchange(self.value);});
	OAT.Instant.assign(this.list);
	
	this.clearOpts = function() {
		OAT.Dom.clear(self.list);
	}
	
	this.addOption = function(name, value) {
		var n = name;
		var v = name;
		if (value) { v = value; }
		var opt = OAT.Dom.create("div",{},"combo_list_option");
		opt.innerHTML = n;
		opt.value = v;
		attach(opt);
		self.list.appendChild(opt);
	}

	var attach = function(option) {
		var ref = function(event) {
			self.value = option.value;
			self.input.value = option.value;
			self.onchange(self.value);
			self.list._Instant_hide();
		}
		OAT.Dom.attach(option,"click",ref);
	}
	
	for (var i=0;i<optList.length;i++) {
		this.addOption(optList[i]);
	}
	
	OAT.Dom.append([self.div,self.input,self.img],[document.body,self.list]);
	
	var showRef = function(event) {
		var coords = OAT.Dom.position(self.input);
		var dims = OAT.Dom.getWH(self.input);
		self.list.style.left = coords[0] +"px";
		self.list.style.top = (coords[1]+dims[1])+"px";
		self.list._Instant_show();
	}
	OAT.Dom.attach(this.img,"click",showRef);
}
OAT.Loader.featureLoaded("combolist");
