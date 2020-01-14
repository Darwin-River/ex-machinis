/**
 * First we will load all of this project's JavaScript dependencies which
 * includes Vue and other libraries. It is a great starting point when
 * building robust, powerful web applications using Vue and Laravel.
 */

require('./bootstrap');

window.Vue = require('vue');


/**
 * Next, we will create a fresh Vue application instance and attach it to
 * the page. Then, you may begin adding components to this application
 * or customize the JavaScript scaffolding to fit your unique needs.
 */

Vue.component('space-objects-table', require('./components/SpaceObjectsTable.vue').default);
Vue.component('spacecraft-table', require('./components/SpacecraftTable.vue').default);
Vue.component('protocols-table', require('./components/ProtocolsTable.vue').default);
Vue.component('query-commands-table', require('./components/QueryCommandsTable.vue').default);

const app = new Vue({
    el: '#app'
});
