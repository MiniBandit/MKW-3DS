import {chromium} from 'playwright-core';
import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
const [executablePath,html,output]=process.argv.slice(2);
const browser=await chromium.launch({executablePath,headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
try {
 const page=await browser.newPage();
 page.on('pageerror',error=>process.stderr.write(String(error)+'\n'));
 await page.goto(pathToFileURL(html).href,{waitUntil:'load',timeout:900000});
 const result=await page.locator('#result').textContent({timeout:900000});
 fs.writeFileSync(output,Buffer.from(result,'base64'));
} finally {await browser.close();}
