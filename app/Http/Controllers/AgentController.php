<?php

namespace App\Http\Controllers;

use App\Agent;
use App\Event;
use App\EventType;
use App\Protocol;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Config;
use Illuminate\Support\Facades\DB;
use Illuminate\View\View;

class AgentController extends Controller
{
    /**
     * Agents search engine
     */
    public function index()
    {
        return view('spacecraft.index');
    }

    /**
     * Used by search engine to retrieve agents
     * @param $request Request object
     * @return JsonResponse
     */
    public function agentsSearch(Request $request)
    {
        $resultsPerPage = Config::get('constants.options.results_per_page');
        $query = DB::table('agents')->select('agents.agent_id as agent_id', 'agents.name as name', 'hulls.id as hull_id', 'hulls.name as hull_name', 'users.name as owner_name'
            , 'users.user_id as owner_id',
            'objects.object_id AS location_id', 'objects.object_name AS location_name')
            ->leftJoin('hulls', 'agents.hull_type', '=', 'hulls.id')
            ->leftJoin('users', 'agents.user_id', '=', 'users.user_id')
            ->leftJoin('objects', 'agents.object_id', '=', 'objects.object_id');
        if ($request->get('agent_id') !== null)
            $query->where('agents.agent_id', intval($request->get('agent_id')));
        if ($request->get('agent_name') !== null)
            $query->where('agents.name', 'LIKE', "%" . $request->get('agent_name') . "%");
        if ($request->get('sort')) {
            $sortParts = explode('|', $request->get('sort'));
            $query->orderBy($sortParts[0], $sortParts[1]);
        }
        $spaceObjects = $query->paginate($resultsPerPage);

        return response()->json($spaceObjects, 200);
    }


    /**
     * Displays one Agent
     * @param integer $id ship's id
     * @return View
     */
    public function view($id)
    {
        $agent = Agent::where('agent_id', '=', $id)->with('hull')->with('user')->with('location')->with('lastEvents')->first();
        if (!$agent)
            abort(404);

        $cargoManifest = DB::table('events')->select(DB::raw('(resources.id) as resource_id'), DB::raw('(resources.name) as name'), DB::raw('(events.locked) as locked'), DB::raw('(resources.mass) as mass')
            , DB::raw('(events.new_quantity) as new_quantity'), DB::raw('(events.id) as event_id'))
            ->leftJoin('resources', 'events.resource', '=', 'resources.id')
            ->leftJoin('event_types', 'events.event_type', '=', 'event_types.id')
            ->join(DB::raw('(SELECT MAX(events.id) AS id FROM events  WHERE drone = ' . $id . ' AND events.outcome = 1 AND (event_type = ' . EventType::TYPE_INCREMENT_INVENTORY . ' OR event_type = ' . EventType::TYPE_DECREMENT_INVENTORY . ')   GROUP BY events.resource ORDER BY id DESC) AS events_latest '), "events_latest.id", '=', 'events.id')//aggregation join
            ->orderBy('new_quantity', 'desc')->havingRaw('new_quantity >= 0')->get();
        $totalWeight = 0;
        foreach ($cargoManifest as $cargoItem) {
            if ($cargoItem->mass && $cargoItem->new_quantity)
                $totalWeight += $cargoItem->mass * $cargoItem->new_quantity;
        }

        /*  echo DB::table('events')->select(DB::raw('(resources.id) as resource_id'), DB::raw('(resources.name) as name'), DB::raw('(events.locked) as locked'), DB::raw('(resources.mass) as mass')
              , DB::raw('(events.new_quantity) as new_quantity'), DB::raw('(events.id) as event_id'))
              ->leftJoin('resources', 'events.resource', '=', 'resources.id')
              ->leftJoin('event_types', 'events.event_type', '=', 'event_types.id')
              ->join(DB::raw('(SELECT MAX(events.id) AS id FROM events  WHERE drone = ' . $id . ' AND events.outcome = 1 AND (event_type = ' . EventType::TYPE_INCREMENT_INVENTORY . ' OR event_type = ' . EventType::TYPE_DECREMENT_INVENTORY . ')   GROUP BY events.resource ORDER BY id DESC) AS events_latest '), "events_latest.id", '=', 'events.id')//aggregation join
  ->orderBy('new_quantity', 'desc')->havingRaw('new_quantity > 0')->toSql();*/
        return view('spacecraft.view', compact('agent', 'cargoManifest', 'totalWeight'));
    }


    /**
     * Displays list of protocols
     * @param $request Request
     * @return View
     */
    public function protocols(Request $request)
    {
        $protocols = Protocol::orderBy('id', 'asc')->get();
        $keyword = $request->get('keyword');
        return view('spacecraft.protocols', compact('protocols','keyword'));
    }
}
