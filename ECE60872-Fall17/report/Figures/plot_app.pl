#proc page
#if @DEVICE in png,gif
   scale: 1
#endif

#proc areadef
   rectangle: 1 1 10 7
   xrange: 1 5
   yrange: 0 2.5

#proc xaxis
   label: Number of Images Uploaded
   labeldistance: 1.0
   selflocatingstubs: text
      1        100
      2        200
      3        300
      4        400
      5        500
     
#proc yaxis
   label: Avg. Response Time (sec)
   labeldistance: 1.0
   stubs: inc 0.5

#proc getdata
file: ../Data/data_app.tab
fieldnames: loc T1 T2 T3 T4 T2-F1 T3-F1 T3-F2

#proc lineplot
    xfield: loc
    yfield: T1
    linedetails: width=3 style=0 dashscale=7
    pointsymbol: shape=square radius=0.1 style=filled
    legendlabel: T1
    legendsampletype: line+symbol
#proc lineplot
    xfield: loc
    yfield: T2
    linedetails: width=3 style=0 dashscale=7
    pointsymbol: shape=circle radius=0.1 style=filled
    legendlabel: T2
    legendsampletype: line+symbol
#proc lineplot
    xfield: loc
    yfield: T3
    linedetails: width=3 style=0 dashscale=7
    pointsymbol: shape=diamond radius=0.1 style=filled
    legendlabel: T3
    legendsampletype: line+symbol
#proc lineplot
    xfield: loc
    yfield: T4
    linedetails: width=3 style=0 dashscale=7
    pointsymbol: shape=triangle radius=0.1 style=filled
    legendlabel: T4
    legendsampletype: line+symbol

#proc lineplot
    xfield: loc
    yfield: T2-F1
    linedetails: color=gray(0.3) width=3 style=1 dashscale=7
    pointsymbol: shape=downtriangle color=gray(0.3) radius=0.1 style=filled
    legendlabel: T2-F1
    legendsampletype: line+symbol
#proc lineplot
    xfield: loc
    yfield: T3-F1
    linedetails: color=gray(0.3) width=3 style=3 dashscale=7
    pointsymbol: shape=triangle color=gray(0.3) radius=0.1 style=filled
    legendlabel: T3-F1
    legendsampletype: line+symbol
#proc lineplot
    xfield: loc
    yfield: T3-F2
    linedetails: color=gray(0.3) width=3 style=9 dashscale=7
    pointsymbol: shape=circle color=gray(0.3) radius=0.1 style=filled
    legendlabel: T3-F2
    legendsampletype: line+symbol
 
#proc legend
  format: across
  textdetails: size=16
  location: min+2.0 min+6
  seglen: 1.0
  separation: 0.8
  noclear: yes
  specifyorder: T1
  		T2
		T3
		T4

#proc legend
  format: across
  textdetails: size=16
  location: min+2.0 min+5
  seglen: 1.0
  separation: 0.4
  noclear: yes
  specifyorder:
		T2-F1
		T3-F1
		T3-F2
