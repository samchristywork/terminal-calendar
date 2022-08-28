let node=document.querySelector("#node")

let weekday_names=[
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
]

let days=[]
let weekdays={}

for (let entry in data){
  let date=new Date(entry)
  date.setTime(date.getTime() + date.getTimezoneOffset()*60*1000);
  if(!isNaN(date)){
    days.push({
      "date":date,
      "data":data[entry].data
    })
  }else{
    weekdays[entry]=data[entry].data
  }
}

days=days.sort((a,b)=>{return a.date>b.date})

let html=""
for (let d of days){
  let today=new Date()
  if(
    d.date.getYear()==today.getYear() &&
    d.date.getMonth()==today.getMonth() &&
    d.date.getDate()==today.getDate()
  ){
    html+="<div id=today style='color:red'><pre>"
  }else{
    html+="<div><pre>"
  }
  html+=d.date
  html+="<br>"
  html+=d.data
  html+=weekdays[weekday_names[d.date.getDay()]]
  html+="</pre></div>"
  html+="<br>"
}
node.innerHTML=html

document.querySelector("#today").scrollIntoView()
